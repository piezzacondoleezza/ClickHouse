#include <Common/config.h>

#if USE_AWS_S3

#    include <IO/WriteBufferFromS3.h>
#    include <IO/WriteHelpers.h>

#    include <aws/s3/S3Client.h>
#    include <aws/s3/model/CreateMultipartUploadRequest.h>
#    include <aws/s3/model/CompleteMultipartUploadRequest.h>
#    include <aws/s3/model/PutObjectRequest.h>
#    include <aws/s3/model/UploadPartRequest.h>
#    include <base/logger_useful.h>

#    include <utility>


namespace ProfileEvents
{
    extern const Event S3WriteBytes;
}


namespace DB
{
// S3 protocol does not allow to have multipart upload with more than 10000 parts.
// In case server does not return an error on exceeding that number, we print a warning
// because custom S3 implementation may allow relaxed requirements on that.
const int S3_WARN_MAX_PARTS = 10000;


namespace ErrorCodes
{
    extern const int S3_ERROR;
}

struct WriteBufferFromS3::UploadPartTask
{
    Aws::S3::Model::UploadPartRequest req;
    bool is_finised = false;
    std::string tag;
    std::exception_ptr exception;
};

struct WriteBufferFromS3::PutObjectTask
{
    Aws::S3::Model::PutObjectRequest req;
    bool is_finised = false;
    std::exception_ptr exception;
};

WriteBufferFromS3::WriteBufferFromS3(
    std::shared_ptr<Aws::S3::S3Client> client_ptr_,
    const String & bucket_,
    const String & key_,
    size_t minimum_upload_part_size_,
    size_t max_single_part_upload_size_,
    std::optional<std::map<String, String>> object_metadata_,
    size_t buffer_size_,
    ThreadPool * thread_pool_)
    : BufferWithOwnMemory<WriteBuffer>(buffer_size_, nullptr, 0)
    , bucket(bucket_)
    , key(key_)
    , object_metadata(std::move(object_metadata_))
    , client_ptr(std::move(client_ptr_))
    , minimum_upload_part_size(minimum_upload_part_size_)
    , max_single_part_upload_size(max_single_part_upload_size_)
    , thread_pool(thread_pool_)
{
    allocateBuffer();
}

void WriteBufferFromS3::nextImpl()
{
    if (!offset())
        return;

    temporary_buffer->write(working_buffer.begin(), offset());

    ProfileEvents::increment(ProfileEvents::S3WriteBytes, offset());

    last_part_size += offset();

    /// Data size exceeds singlepart upload threshold, need to use multipart upload.
    if (multipart_upload_id.empty() && last_part_size > max_single_part_upload_size)
        createMultipartUpload();

    if (!multipart_upload_id.empty() && last_part_size > minimum_upload_part_size)
    {
        writePart();
        allocateBuffer();
    }

    waitForReadyBackGroundTasks();
}

void WriteBufferFromS3::allocateBuffer()
{
    temporary_buffer = Aws::MakeShared<Aws::StringStream>("temporary buffer");
    temporary_buffer->exceptions(std::ios::badbit);
    last_part_size = 0;
}

WriteBufferFromS3::~WriteBufferFromS3()
{
    finalize();
}

void WriteBufferFromS3::preFinalize()
{
    next();

    if (multipart_upload_id.empty())
    {
        makeSinglepartUpload();
    }
    else
    {
        /// Write rest of the data as last part.
        writePart();
    }

    is_prefinalized = true;
}

void WriteBufferFromS3::finalizeImpl()
{
    if (!is_prefinalized)
        preFinalize();

    waitForAllBackGroundTasks();

    if (!multipart_upload_id.empty())
        completeMultipartUpload();
}

void WriteBufferFromS3::createMultipartUpload()
{
    Aws::S3::Model::CreateMultipartUploadRequest req;
    req.SetBucket(bucket);
    req.SetKey(key);
    if (object_metadata.has_value())
        req.SetMetadata(object_metadata.value());

    auto outcome = client_ptr->CreateMultipartUpload(req);

    if (outcome.IsSuccess())
    {
        multipart_upload_id = outcome.GetResult().GetUploadId();
        LOG_DEBUG(log, "Multipart upload has created. Bucket: {}, Key: {}, Upload id: {}", bucket, key, multipart_upload_id);
    }
    else
        throw Exception(outcome.GetError().GetMessage(), ErrorCodes::S3_ERROR);
}

void WriteBufferFromS3::writePart()
{
    auto size = temporary_buffer->tellp();

    LOG_DEBUG(log, "Writing part. Bucket: {}, Key: {}, Upload_id: {}, Size: {}", bucket, key, multipart_upload_id, size);

    if (size < 0)
        throw Exception("Failed to write part. Buffer in invalid state.", ErrorCodes::S3_ERROR);

    if (size == 0)
    {
        LOG_DEBUG(log, "Skipping writing part. Buffer is empty.");
        return;
    }

    if (part_tags.size() == S3_WARN_MAX_PARTS)
    {
        // Don't throw exception here by ourselves but leave the decision to take by S3 server.
        LOG_WARNING(log, "Maximum part number in S3 protocol has reached (too many parts). Server may not accept this whole upload.");
    }

    if (thread_pool)
    {
        UploadPartTask * task = nullptr;
        int part_number;
        {
            std::lock_guard lock(bg_tasks_mutex);
            task = &upload_object_tasks.emplace_back();
            ++num_added_bg_tasks;
            part_number = num_added_bg_tasks;
        }

        fillUploadRequest(task->req, part_number);
        thread_pool->scheduleOrThrow([this, task]()
        {
            try
            {
                processUploadRequest(*task);
            }
            catch (...)
            {
                task->exception = std::current_exception();
            }

            {
                std::lock_guard lock(bg_tasks_mutex);
                task->is_finised = true;
                ++num_finished_bg_tasks;
            }

            bg_tasks_condvar.notify_one();
        });
    }
    else
    {
        UploadPartTask task;
        fillUploadRequest(task.req, part_tags.size() + 1);
        processUploadRequest(task);
        part_tags.push_back(task.tag);
    }
}

void WriteBufferFromS3::fillUploadRequest(Aws::S3::Model::UploadPartRequest & req, int part_number)
{
    req.SetBucket(bucket);
    req.SetKey(key);
    req.SetPartNumber(part_number);
    req.SetUploadId(multipart_upload_id);
    req.SetContentLength(temporary_buffer->tellp());
    req.SetBody(temporary_buffer);
}

void WriteBufferFromS3::processUploadRequest(UploadPartTask & task)
{
    auto outcome = client_ptr->UploadPart(task.req);

    if (outcome.IsSuccess())
    {
        task.tag = outcome.GetResult().GetETag();
        LOG_DEBUG(log, "Writing part finished. Bucket: {}, Key: {}, Upload_id: {}, Etag: {}, Parts: {}", bucket, key, multipart_upload_id, task.tag, part_tags.size());
    }
    else
        throw Exception(outcome.GetError().GetMessage(), ErrorCodes::S3_ERROR);
}

void WriteBufferFromS3::completeMultipartUpload()
{
    LOG_DEBUG(log, "Completing multipart upload. Bucket: {}, Key: {}, Upload_id: {}, Parts: {}", bucket, key, multipart_upload_id, part_tags.size());

    if (part_tags.empty())
        throw Exception("Failed to complete multipart upload. No parts have uploaded", ErrorCodes::S3_ERROR);

    Aws::S3::Model::CompleteMultipartUploadRequest req;
    req.SetBucket(bucket);
    req.SetKey(key);
    req.SetUploadId(multipart_upload_id);

    Aws::S3::Model::CompletedMultipartUpload multipart_upload;
    for (size_t i = 0; i < part_tags.size(); ++i)
    {
        Aws::S3::Model::CompletedPart part;
        multipart_upload.AddParts(part.WithETag(part_tags[i]).WithPartNumber(i + 1));
    }

    req.SetMultipartUpload(multipart_upload);

    auto outcome = client_ptr->CompleteMultipartUpload(req);

    if (outcome.IsSuccess())
        LOG_DEBUG(log, "Multipart upload has completed. Bucket: {}, Key: {}, Upload_id: {}, Parts: {}", bucket, key, multipart_upload_id, part_tags.size());
    else
    {
        std::string tags_str;
        for (auto & str : part_tags)
            tags_str += ' ' + str;
        throw Exception(ErrorCodes::S3_ERROR, "{} Tags:{}", outcome.GetError().GetMessage(), tags_str);
    }
}

void WriteBufferFromS3::makeSinglepartUpload()
{
    auto size = temporary_buffer->tellp();
    bool with_pool = thread_pool != nullptr;

    LOG_DEBUG(log, "Making single part upload. Bucket: {}, Key: {}, Size: {}, WithPool: {}", bucket, key, size, with_pool);

    if (size < 0)
        throw Exception("Failed to make single part upload. Buffer in invalid state", ErrorCodes::S3_ERROR);

    if (size == 0)
    {
        LOG_DEBUG(log, "Skipping single part upload. Buffer is empty.");
        return;
    }

    if (thread_pool)
    {
        put_object_task = std::make_unique<PutObjectTask>();
        fillPutRequest(put_object_task->req);
        thread_pool->scheduleOrThrow([this]()
        {
            try
            {
                processPutRequest(*put_object_task);
            }
            catch (...)
            {
                put_object_task->exception = std::current_exception();
            }

            {
                std::lock_guard lock(bg_tasks_mutex);
                put_object_task->is_finised = true;
            }

            bg_tasks_condvar.notify_one();
        });
    }
    else
    {
        PutObjectTask task;
        fillPutRequest(task.req);
        processPutRequest(task);
    }
}

void WriteBufferFromS3::fillPutRequest(Aws::S3::Model::PutObjectRequest & req)
{
    req.SetBucket(bucket);
    req.SetKey(key);
    req.SetContentLength(temporary_buffer->tellp());
    req.SetBody(temporary_buffer);
    if (object_metadata.has_value())
        req.SetMetadata(object_metadata.value());
}

void WriteBufferFromS3::processPutRequest(PutObjectTask & task)
{
    auto outcome = client_ptr->PutObject(task.req);
    bool with_pool = thread_pool != nullptr;

    if (outcome.IsSuccess())
        LOG_DEBUG(log, "Single part upload has completed. Bucket: {}, Key: {}, Object size: {}, WithPool: {}", bucket, key, task.req.GetContentLength(), with_pool);
    else
        throw Exception(outcome.GetError().GetMessage(), ErrorCodes::S3_ERROR);
}

void WriteBufferFromS3::waitForReadyBackGroundTasks()
{
    if (thread_pool)
    {
        std::lock_guard lock(bg_tasks_mutex);
        {
            while (!upload_object_tasks.empty() && upload_object_tasks.front().is_finised)
            {
                auto & task = upload_object_tasks.front();
                auto exception = std::move(task.exception);
                auto tag = std::move(task.tag);
                upload_object_tasks.pop_front();

                if (exception)
                {
                    waitForAllBackGroundTasks();
                    std::rethrow_exception(exception);
                }

                part_tags.push_back(tag);
            }
        }
    }
}

void WriteBufferFromS3::waitForAllBackGroundTasks()
{
    if (thread_pool)
    {
        std::unique_lock lock(bg_tasks_mutex);
        bg_tasks_condvar.wait(lock, [this]() { return num_added_bg_tasks == num_finished_bg_tasks; });

        while (!upload_object_tasks.empty())
        {
            auto & task = upload_object_tasks.front();
            if (task.exception)
                std::rethrow_exception(std::move(task.exception));

            part_tags.push_back(task.tag);

            upload_object_tasks.pop_front();
        }

        if (put_object_task)
        {
            bg_tasks_condvar.wait(lock, [this]() { return put_object_task->is_finised; });
            if (put_object_task->exception)
                std::rethrow_exception(std::move(put_object_task->exception));
        }
    }
}

}

#endif
