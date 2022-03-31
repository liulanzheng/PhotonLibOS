#include <fcntl.h>
#include <fuse/fuse_opt.h>
#include <sys/stat.h>

#include <cstdio>
#include <thread>

#include "common/alog.h"
#include "common/executor/executor.h"
#include "io/aio-wrapper.h"
#include "io/fd-events.h"
#include "thread/thread.h"
#include "../aligned-file.h"
#include "../async_filesystem.h"
#include "../exportfs.h"
#include "../fuse_adaptor.h"
#include "../localfs.h"

enum { KEY_SRC, KEY_IOENGINE };

struct localfs_config {
    char *src;
    char *ioengine;
    char *exportfs;
};

#define MYFS_OPT(t, p, v) \
    { t, offsetof(struct localfs_config, p), v }
struct fuse_opt localfs_opts[] = {MYFS_OPT("src=%s", src, 0), MYFS_OPT("ioengine=%s", ioengine, 0),
                                  MYFS_OPT("exportfs=%s", exportfs, 0), FUSE_OPT_END};

// this simple fuse test MUST run with -s (single thread)
int main(int argc, char *argv[]) {
    // currently they will be initialized inside fuser_go
    // photon::fd_events_init();
    // photon::libaio_wrapper_init();
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct localfs_config cfg;
    fuse_opt_parse(&args, &cfg, localfs_opts, NULL);
    int ioengine = FileSystem::ioengine_libaio;
    if (cfg.ioengine) {
        switch (*cfg.ioengine) {
            case 'p':
                ioengine = FileSystem::ioengine_psync;
                break;
            case 'l':
                ioengine = FileSystem::ioengine_libaio;
                break;
            case 'a':
                ioengine = FileSystem::ioengine_posixaio;
                break;
            default:
                LOG_ERROR_RETURN(EINVAL, -1, "Invalid ioengine ", cfg.ioengine);
        }
    }
    if (cfg.src == nullptr) LOG_ERROR_RETURN(EINVAL, -1, "Invalid source folder");
    LOG_DEBUG(VALUE(cfg.src));
    LOG_DEBUG(VALUE(cfg.ioengine));
    LOG_DEBUG(VALUE(cfg.exportfs));
    if (cfg.exportfs && *cfg.exportfs == 't') {
        auto fs = FileSystem::new_localfs_adaptor(cfg.src, ioengine);
        auto wfs = FileSystem::new_aligned_fs_adaptor(fs, 4096, true, true);
        return fuser_go_exportfs(wfs, args.argc, args.argv);
    } else if (cfg.exportfs && *cfg.exportfs == 'c') {
        Executor::HybridEaseExecutor eth;
        auto afs = eth.perform([&]() {
            auto fs = FileSystem::new_localfs_adaptor(cfg.src, ioengine);
            auto wfs = FileSystem::new_aligned_fs_adaptor(fs, 4096, true, true);
            FileSystem::exportfs_init();
            return export_as_sync_fs(wfs);
        });
        umask(0);
        set_fuse_fs(afs);
        auto oper = get_fuse_xmp_oper();
        return fuse_main(args.argc, args.argv, oper, NULL);
    } else {
        auto fs = FileSystem::new_localfs_adaptor(cfg.src, ioengine);
        auto wfs = FileSystem::new_aligned_fs_adaptor(fs, 4096, true, true);
        return fuser_go(wfs, args.argc, args.argv);
    }
}