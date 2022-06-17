#include <srpc/common.h>
#include <sysrepo.h>

#include <fcntl.h>
#include <sys/sendfile.h>

/**
 * Check wether the datastore contains any data or not based on the provided path to check.
 *
 * @param session Sysrepo session to the datastore to check.
 * @param path Path to the data for checking.
 * @param empty Boolean value to set.
 *
 * @return Error code - 0 on success.
 */
int srpc_check_empty_datastore(sr_session_ctx_t *session, const char *path, bool *empty)
{
    int error = 0;
    bool is_empty = true;
    sr_val_t *values = NULL;
    size_t value_cnt = 0;

    error = sr_get_items(session, path, 0, SR_OPER_DEFAULT, &values, &value_cnt);
    if (error)
    {
        goto error_out;
    }

    if (value_cnt > 0)
    {
        is_empty = false;

        // free recieved values
        sr_free_values(values, value_cnt);
    }

    *empty = is_empty;

    goto out;

error_out:
    error = -1;

out:
    return error;
}

/**
 * Iterate changes for the provided xpath and use callback on each change.
 *
 * @param priv Private user data - pass plugin context.
 * @param session Sysrepo session to use for iteration.
 * @param xpath XPath for the changes iterator.
 * @param cb Callback to call on each change.
 *
 * @return Error code - 0 on success.
 */
int srpc_iterate_changes(void *priv, sr_session_ctx_t *session, const char *xpath, srpc_change_cb cb)
{
    int error = 0;

    // sysrepo
    sr_change_iter_t *changes_iterator = NULL;
    sr_change_oper_t operation = SR_OP_CREATED;
    const char *prev_value = NULL, *prev_list = NULL;
    int prev_default;

    srpc_change_ctx_t change_ctx;

    // libyang
    const struct lyd_node *node = NULL;

    error = sr_get_changes_iter(session, xpath, &changes_iterator);
    if (error != SR_ERR_OK)
    {
        error = -1;
        goto out;
    }

    int counter = 2;

    while (sr_get_change_tree_next(session, changes_iterator, &change_ctx.operation, &change_ctx.node,
                                   &change_ctx.previous_value, &change_ctx.previous_list,
                                   &change_ctx.previous_default) == SR_ERR_OK)
    {
        error = cb(priv, session, &change_ctx);
        if (error)
        {
            // return number of invalid callback
            error = -counter;
            goto out;
        }
        ++counter;
    }

out:
    // free iterator data
    sr_free_change_iter(changes_iterator);

    return 0;
}

/**
 * Copy file from source to destination.
 *
 * @param source Source file path.
 * @param destination Destination file path.
 *
 * @return Error code - 0 on success.
 */
int srpc_copy_file(const char *source, const char *destination)
{
    int error = 0;
    int read_fd = -1;
    int write_fd = -1;
    struct stat stat_buf = {0};
    off_t offset = 0;

    read_fd = open(source, O_RDONLY);
    if (read_fd == -1)
    {
        goto error_out;
    }

    if (fstat(read_fd, &stat_buf) != 0)
    {
        goto error_out;
    }

    write_fd = open(destination, O_CREAT | O_WRONLY | O_TRUNC, stat_buf.st_mode);
    if (write_fd == -1)
    {
        goto error_out;
    }

    if (sendfile(write_fd, read_fd, &offset, (size_t)stat_buf.st_size) == -1)
    {
        goto error_out;
    }

    goto out;

error_out:
    error = -1;

out:
    if (read_fd != -1)
    {
        close(read_fd);
    }

    if (write_fd != -1)
    {
        close(write_fd);
    }

    return error;
}