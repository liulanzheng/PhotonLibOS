#pragma once

namespace photon {
int run_fuse(int argc, char *argv[], const struct fuse_operations *op,
                void *user_data);
}