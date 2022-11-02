#!/bin/bash
# $$PHOTON_UNPUBLISHED_FILE$$

# end-of-jobs marker
job_pool_end_of_jobs="JOBPOOL_END_OF_JOBS"

# job queue used to send jobs to the workers
job_pool_job_queue=/tmp/job_pool_job_queue_$$

# where to run results to
job_pool_result_log=/tmp/job_pool_result_log_$$

# toggle command echoing
job_pool_echo_command=0

# number of parallel jobs allowed.  also used to determine if job_pool_init
# has been called when jobs are queued.
job_pool_pool_size=-1

# \brief variable to check for number of non-zero exits
job_pool_nerrors=0

################################################################################
# private functions
################################################################################

# \brief debug output
function _job_pool_echo()
{
    if [[ "${job_pool_echo_command}" == "1" ]]; then
        echo $@
    fi
}

# \brief cleans up
function _job_pool_cleanup()
{
    rm -f ${job_pool_job_queue} ${job_pool_result_log}
}

# \brief signal handler
function _job_pool_exit_handler()
{
    _job_pool_stop_workers
    _job_pool_cleanup
}

# \brief print the exit codes for each command
# \param[in] result_log  the file where the exit codes are written to
function _job_pool_print_result_log()
{
    job_pool_nerrors=$(grep ^ERROR "${job_pool_result_log}" | wc -l)
    cat "${job_pool_result_log}" | sed -e 's/^ERROR//'
}

# \brief the worker function that is called when we fork off worker processes
# \param[in] id  the worker ID
# \param[in] job_queue  the fifo to read jobs from
# \param[in] result_log  the temporary log file to write exit codes to
function _job_pool_worker()
{
    local id=$1
    local job_queue=$2
    local result_log=$3
    local cmd=
    local args=

    exec 7<> ${job_queue}
    while [[ "${cmd}" != "${job_pool_end_of_jobs}" && -e "${job_queue}" ]]; do
        # workers block on the exclusive lock to read the job queue
        flock --exclusive 7
        IFS=$'\v'
        read cmd args <${job_queue}
        set -- ${args}
        unset IFS
        flock --unlock 7
        # the worker should exit if it sees the end-of-job marker or run the
        # job otherwise and save its exit code to the result log.
        if [[ "${cmd}" == "${job_pool_end_of_jobs}" ]]; then
            # write it one more time for the next sibling so that everyone
            # will know we are exiting.
            echo "${cmd}" >&7
        else
            _job_pool_echo "### _job_pool_worker-${id}: ${cmd}"
            # run the job
            {
                sudo docker run \
                    --rm -v $PWD:$PWD -v $HOME/.dep_create_cache:$HOME/.dep_create_cache \
                    --name "para_ut.$$.$id$(echo "$cmd" | tr '/' '-')" \
                    reg.docker.alibaba-inc.com/cise/cise_7u2:latest \
                    -c "cd $PWD; ${cmd} $@"  > "$cmd.runlog" 2>&1 ;

            }
            # now check the exit code and prepend "ERROR" to the result log entry
            # which we will use to count errors and then strip out later.
            local result=$?
            local status=
            if [[ "${result}" != "0" ]]; then
                status="ERROR"
            fi
            # now write the error to the log, making sure multiple processes
            # don't trample over each other.
            exec 8<> ${result_log}
            flock --exclusive 8
            _job_pool_echo "${status} job_pool: exited ${result}: ${cmd} $@" >> ${result_log}
            flock --unlock 8
            exec 8>&-
            _job_pool_echo "### _job_pool_worker-${id}: exited ${result}: ${cmd} $@"
        fi
    done
    exec 7>&-
}

# \brief sends message to worker processes to stop
function _job_pool_stop_workers()
{
    # send message to workers to exit, and wait for them to stop before
    # doing cleanup.
    echo ${job_pool_end_of_jobs} >> ${job_pool_job_queue}
    wait
}

# \brief fork off the workers
# \param[in] job_queue  the fifo used to send jobs to the workers
# \param[in] result_log  the temporary log file to write exit codes to
function _job_pool_start_workers()
{
    local job_queue=$1
    local result_log=$2
    for ((i=0; i<${job_pool_pool_size}; i++)); do
        _job_pool_worker ${i} ${job_queue} ${result_log} &
    done
}

################################################################################
# public functions
################################################################################

# \brief initializes the job pool
# \param[in] pool_size  number of parallel jobs allowed
# \param[in] echo_command  1 to turn on echo, 0 to turn off
function job_pool_init()
{
    local pool_size=$1
    local echo_command=$2

    # set the global attibutes
    job_pool_pool_size=${pool_size:=1}
    job_pool_echo_command=${echo_command:=0}

    # create the fifo job queue and create the exit code log
    rm -rf ${job_pool_job_queue} ${job_pool_result_log}
    mkfifo ${job_pool_job_queue}
    touch ${job_pool_result_log}

    # fork off the workers
    _job_pool_start_workers ${job_pool_job_queue} ${job_pool_result_log}
}

# \brief waits for all queued up jobs to complete and shuts down the job pool
function job_pool_shutdown()
{
    _job_pool_stop_workers
    _job_pool_print_result_log
    _job_pool_cleanup
}

# \brief run a job in the job pool
function job_pool_run()
{
    if [[ "${job_pool_pool_size}" == "-1" ]]; then
        job_pool_init
    fi
    printf "%s\v" "$@" >> ${job_pool_job_queue}
    echo >> ${job_pool_job_queue}
}

# \brief waits for all queued up jobs to complete before starting new jobs
# This function actually fakes a wait by telling the workers to exit
# when done with the jobs and then restarting them.
function job_pool_wait()
{
    _job_pool_stop_workers
    _job_pool_start_workers ${job_pool_job_queue} ${job_pool_result_log}
}
#########################################
# End of Job Pool
#########################################


# default config
MODE=debug
BUILD=0
BUILD_TEST=0
CLEAN=0
DIST=0
COVERAGE=
JOBS=8
RUN_TEST=0
NO_DOCKER=0

__usage="
Usage: $(basename $0) [OPTIONS]
Options:
 -b, --build [debug|release]        Build mode, DEBUG or RELEASE, default
                                    is debug.
 -c, --clean                        Clean before build
 -t, --test                         Build test tasks
 -o, --coverage                     Build with coverage
 -j, --jobs                         Compile threads
 -r, --runtest                      Run all fast tests
 -d, --dist                         Pack library into ./package directory.
                                    This directory will be clean at first,
                                    libease will be collected into this
                                    directory, and debug symbols will be
                                    stripped and save into ./package/symbols
 --no_docker                        If set, test task will not running in docker
                                    container, but run by alimake.
 -h, --help                         Print usage information.

Sample:
 $(basename $0) -b release -c -d    Clean and build release libease,
                                    package library and debug symbol into
                                    package directory.
"

function usage() {
    echo "$__usage"
}

function cppack {
    cp build/$MODE/$1.so build/$MODE/$1.a package/
    bn=$(basename $1.so)
    objcopy --only-keep-debug package/$bn package/$bn.debug
    strip --strip-debug package/$bn
    objcopy --add-gnu-debuglink=package/$bn.debug package/$bn
}

TEMP=`getopt -o b:cdtorhj: -l test,build:,test,clean,dist,coverage,runtest,help,jobs,no_docker \
    -n 'build.sh' -- "$@"`

if [ $? != 0 ] ; then echo "Terminating..." >&2 ; exit 1 ; fi

eval set -- "$TEMP"
while true ; do
    case "$1" in
        -b|--build)
            BUILD=1
            case "$2" in
                d|debug) MODE=debug ; shift 2;;
                r|release) MODE=release; DIST=1; shift 2;;
                *) usage; exit 0;;
            esac;;
        -c|--clean)
            CLEAN=1; shift ;;
        -t|--test)
            BUILD=1; BUILD_TEST=1; shift ;;
        -d|--dist)
            BUILD=1; DIST=1; shift ;;
        -o|--coverage)
            BUILD=1; COVERAGE=--coverage; shift ;;
        -j|--jobs)
            JOBS=$2 ; shift 2;;
        -r|--runtest)
            RUN_TEST=1; shift ;;
        --no_docker)
            NO_DOCKER=1; shift ;;
        -h|--help)
            usage; exit 0;;
        --)
            shift ; break ;;
        *)
            echo 'wtf'
            usage ; exit 1 ;;
    esac
done

if [[ $CLEAN == 1 ]]; then
    alimake -a clean -b $MODE $COVERAGE|| exit 1
fi
if [[ $BUILD == 1 ]]; then
    alimake -a prebuild
    alimake -b $MODE -j$JOBS -t photon $* || exit 1
fi
if [[ $BUILD_TEST == 1 ]]; then
    alimake -i test -j$JOBS -b $MODE $COVERAGE -c no-werror $* || exit 1
fi
if [[ $DIST == 1 ]]; then
    echo "Clean old package directory"
    rm -rf package &> /dev/null || exit 1
    mkdir -p package
    echo "Packaging..."
    cppack libphoton
    echo "Done"
fi
if [[ $RUN_TEST == 1 ]]; then
    if [[ $(which docker) == 1 ]]; then
        NO_DOCKER=1
    fi
    if [[ $NO_DOCKER == 0 ]]; then
        echo "Run all tests in container"
        TESTLIST=$(alimake -i test -a list -b $MODE | grep cpp_fast_test | cut -f 2 -d' ')
        echo "$TESTLIST"
        job_pool_init $JOBS 1
        find ./build/$MODE -name *.runlog -delete
        for TASK in $TESTLIST; do
            job_pool_run ./build/$MODE/$TASK
        done
        job_pool_wait
        job_pool_shutdown
        # find ./build/$MODE -name '*.runlog' -print0 | xargs -0 grep -h -E "^\[.{10}\]"
        find ./build/$MODE -name '*.runlog' -exec cat {} +
        echo "Failed tests num $job_pool_nerrors"
        exit $job_pool_nerrors
    else
        alimake -i test -a test -b $MODE -j $JOBS
    fi
fi
