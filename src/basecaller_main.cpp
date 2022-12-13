/**
 * @file basecaller_main.c
 * @brief entry point to basecaller_main
 * @author Hasindu Gamaarachchi (hasindu@unsw.edu.au)

MIT License

Copyright (c) 2019 Hasindu Gamaarachchi (hasindu@unsw.edu.au)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


******************************************************************************/

#include "decode/CPUDecoder.h"
#include "decode/GPUDecoder.h"
#include "utils/stitch.h"
#include "nn/ModelRunner.h"
#include "slorado.h"
#include "error.h"
#include "misc.h"
#include "signal_prep.h"
#include "basecall.h"
#include "writer.h"
#include "misc.h"

#include <assert.h>
#include <cstdint>
#include <getopt.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <torch/torch.h>

static struct option long_options[] = {
    {"threads", required_argument, 0, 't'},         //0 number of threads [8]
    {"batchsize", required_argument, 0, 'K'},       //1 batchsize - number of reads loaded at once [512]
    {"max-bytes", required_argument, 0, 'B'},       //2 batchsize - number of bytes loaded at once
    {"verbose", required_argument, 0, 'v'},         //3 verbosity level [1]
    {"help", no_argument, 0, 'h'},                  //4
    {"version", no_argument, 0, 'V'},               //5
    {"output", required_argument, 0, 'o'},          //6 output to a file [stdout]
    {"debug-break", required_argument, 0, 0},       //7 break after processing the first batch (used for debugging)
    {"profile-cpu", required_argument, 0, 0},       //8 perform section by section (used for profiling - for CPU only)
    {"accel",required_argument, 0, 0},              //9 accelerator
    {"chunk-size", required_argument, 0, 'c'},      //10 chunk size [8000]
    {"overlap", required_argument, 0, 'p'},         //11 overlap [150]
    {"device", required_argument, 0, 'x'},          //12 device [cpu]
    {"num-runners", required_argument, 0, 'r'},     //13 number of runners [1]
    {"emit-fastq", required_argument, 0, 0},        //14 toggles emit fastq
    {0, 0, 0, 0}};


static inline void print_help_msg(FILE *fp_help, opt_t opt){
    fprintf(fp_help, "usage: slorado basecaller [model] [data]\n");
    fprintf(fp_help, "positional arguments:\n");
    fprintf(fp_help, "  model FILE                  the basecaller model to run.\n");
    fprintf(fp_help, "  data FILE                   the data directory.\n");
    fprintf(fp_help, "\nbasic options:\n");
    fprintf(fp_help, "  -t INT                      number of processing threads [%d]\n", opt.num_thread);
    fprintf(fp_help, "  -K INT                      batch size (max number of reads loaded at once) [%d]\n", opt.batch_size); 
    fprintf(fp_help, "  -B FLOAT[K/M/G]             max number of bytes loaded at once [%.1fM]\n", opt.batch_size_bytes/(float)(1000*1000));
    fprintf(fp_help, "  -o FILE                     output to file [%s]\n", opt.out_path);
    fprintf(fp_help, "  -c INT                      chunk size [%d]\n", opt.chunk_size);
    fprintf(fp_help, "  -p INT                      overlap [%d]\n", opt.overlap);
    fprintf(fp_help, "  -x DEVICE                   specify device [%s]\n", opt.device);
    fprintf(fp_help, "  -r INT                      number of runners [%d]\n", opt.num_runners);
    fprintf(fp_help, "  -h                          shows help message and exits\n");   
    fprintf(fp_help, "  --verbose INT               verbosity level [%d]\n",(int)get_log_level());
    fprintf(fp_help, "  --version                   print version\n");
    fprintf(fp_help, "\nadvanced options:\n");
    fprintf(fp_help, "  --debug-break INT           break after processing the specified no. of batches\n");
    fprintf(fp_help, "  --emit-fastq=yes|no         emits fastq output format\n");
    fprintf(fp_help, "  --profile-cpu=yes|no        process section by section (used for profiling on CPU)\n");
#ifdef HAVE_ACC
    fprintf(fp_help,"   --accel=yes|no             Running on accelerator [%s]\n",(opt.flag&SLORADO_ACC?"yes":"no"));
#endif

}

int basecaller_main(int argc, char* argv[]) {

    double array[] = { 1, 2, 3, 4, 5};
    double realtime0 = realtime();

    const char* optstring = "t:B:K:v:o:x:r:p:c:hV";

    int longindex = 0;
    int32_t c = -1;

    char *data = NULL;
    char *model = NULL;

    FILE *fp_help = stderr;

    opt_t opt;
    init_opt(&opt); //initialise options to defaults

    //parse the user args
    while ((c = getopt_long(argc, argv, optstring, long_options, &longindex)) >= 0) {
        if (c == 'B') {
            opt.batch_size_bytes = mm_parse_num(optarg);
            if(opt.batch_size_bytes<=0){
                ERROR("%s","Maximum number of bytes should be larger than 0.");
                exit(EXIT_FAILURE);
            }
        } else if (c == 'K') {
            opt.batch_size = atoi(optarg);
            if (opt.batch_size < 1) {
                ERROR("Batch size should larger than 0. You entered %d",opt.batch_size);
                exit(EXIT_FAILURE);
            }
        } else if (c == 't') {
            opt.num_thread = atoi(optarg);
            if (opt.num_thread < 1) {
                ERROR("Number of threads should larger than 0. You entered %d", opt.num_thread);
                exit(EXIT_FAILURE);
            }
        } else if (c == 'v') {
            int v = atoi(optarg);
            set_log_level((enum log_level_opt)v);
        } else if (c == 'x') {
            opt.device = optarg;
        } else if (c == 'c') {
            opt.chunk_size = atoi(optarg);
            if (opt.chunk_size < 1) {
                ERROR("Chunk size should larger than 0. You entered %d", opt.chunk_size);
                exit(EXIT_FAILURE);
            }
        } else if (c == 'p') {
            opt.overlap = atoi(optarg);
            if (opt.overlap < 1) {
                ERROR("Overlap should larger than 0. You entered %d", opt.overlap);
                exit(EXIT_FAILURE);
            }
        } else if (c == 'o') {
            opt.out_path = optarg;
            opt.out = fopen(opt.out_path, "w");
            if (opt.out == NULL) {
                fprintf(stderr,"Error in opening output file\n");
                exit(EXIT_FAILURE);
            }
        } else if (c == 'r') {
            opt.num_runners = atoi(optarg);
            if (opt.num_runners < 1) {
                ERROR("Number of runners should larger than 0. You entered %d", opt.num_runners);
                exit(EXIT_FAILURE);
            }
        } else if (c == 'V') {
            fprintf(stdout,"slorado %s\n",SLORADO_VERSION);
            exit(EXIT_SUCCESS);
        } else if (c == 'h'){
            fp_help = stdout;
        } else if(c == 0 && longindex == 7) { //debug break
            opt.debug_break = atoi(optarg);
        } else if(c == 0 && longindex == 8) { //sectional benchmark todo : warning for gpu mode
            yes_or_no(&opt.flag, SLORADO_PRF, long_options[longindex].name, optarg, 1);
        } else if(c == 0 && longindex == 9) { //accel
        #ifdef HAVE_ACC
            yes_or_no(&opt.flag, SLORADO_ACC, long_options[longindex].name, optarg, 1);
        #else
            WARNING("%s", "--accel has no effect when compiled for the CPU");
        #endif
        } else if(c == 0 && longindex == 14) { //sectional benchmark todo : warning for gpu mode
            yes_or_no(&opt.flag, SLORADO_EFQ, long_options[longindex].name, optarg, 1);
        }
    }

    // Incorrect number of arguments given
    if (argc - optind != 2 || fp_help == stdout) {
        print_help_msg(fp_help, opt);
        if(fp_help == stdout){
            exit(EXIT_SUCCESS);
        }
        exit(EXIT_FAILURE);
    }

    model = argv[optind++];

    if (model == NULL) {
        print_help_msg(fp_help, opt);
        if(fp_help == stdout){
            exit(EXIT_SUCCESS);
        }
        exit(EXIT_FAILURE);
    }

    data = argv[optind];

    if (data == NULL) {
        print_help_msg(fp_help, opt);
        if(fp_help == stdout){
            exit(EXIT_SUCCESS);
        }
        exit(EXIT_FAILURE);
    }

    // print summary
    fprintf(stderr,"\nslorado base-caller version %s\n", SLORADO_VERSION);
    fprintf(stderr,"model path:         %s\n", model);
    fprintf(stderr,"input path:         %s\n", data);
    fprintf(stderr,"output path:        %s\n", opt.out_path);
    fprintf(stderr,"device:             %s\n", opt.device);
    fprintf(stderr,"chunk size:         %d\n", opt.chunk_size);
    fprintf(stderr,"batch size:         %d\n", opt.batch_size);
    fprintf(stderr,"no. threads:        %d\n", opt.num_thread);
    fprintf(stderr,"no. runners:        %d\n", opt.num_runners);
    fprintf(stderr,"overlap:            %d\n", opt.overlap);
    
    // performance vars
    double time_read = 0;
    double time_tens = 0;
    double time_trim = 0;
    double time_scale = 0;
    double time_chunk = 0;
    double time_basecall = 0;
    double time_decode = 0;
    double time_stitch = 0;
    double time_write = 0;
    
    // open slow5 file
    slow5_file_t *sp = slow5_open(data,"r");
    if (sp==NULL) {
       fprintf(stderr,"Error in opening slow5 file\n");
       exit(EXIT_FAILURE);
    }
    slow5_rec_t *rec = NULL;
    int ret=0;

    // create model runner
    ModelRunner<GPUDecoder> model_runner = ModelRunner<GPUDecoder>(model, opt.device, opt.chunk_size, opt.batch_size);
    
    // total time
    uint64_t n_samples = 0;
    double total_time = -realtime();
    
    while (1) {
        time_read -= realtime();
        ret = slow5_get_next(&rec,sp);
        if(ret < 0){
            if (slow5_errno != SLOW5_ERR_EOF) {
                return EXIT_FAILURE;
            } else { //EOF file reached
                break;
            }
        }
        time_read += realtime();
        
        // convert record to tensor
        time_tens -= realtime();
        torch::Tensor signal = tensor_from_record(rec);
        time_tens += realtime();
        
        n_samples += signal.size(0);

        // trim signal
        time_trim -= realtime();
        int trim_start = trim_signal(signal.index({torch::indexing::Slice(torch::indexing::None, 8000)}));
        signal = signal.index({torch::indexing::Slice(trim_start, torch::indexing::None)});
        time_trim += realtime();
        
        // scale signal
        time_scale -= realtime();
        scale_signal(signal);
        time_scale += realtime();
    
        // split signal into chunks
        time_chunk -= realtime();
        std::vector<Chunk> chunks = chunks_from_tensor(signal, opt.chunk_size, opt.overlap);
        time_chunk += realtime();

        // decode signal
        basecall_chunks(signal, chunks, opt.chunk_size, opt.batch_size, model_runner, time_basecall, time_decode);
    
        // stitch
        time_stitch -= realtime();
        std::string sequence;
        std::string qstring;
        stitch_chunks(chunks, sequence, qstring);
        time_stitch += realtime();

        // print output
        time_write -= realtime();
        write_to_file(opt.out, sequence, qstring, rec->read_id, (opt.flag & SLORADO_EFQ) != 0);
        time_write += realtime();
        
    }
    total_time += realtime();
    
    if (ret != SLOW5_ERR_EOF) {
        fprintf(stderr,"Could not reach end of slow5 file. Error code %d\n",ret);
        exit(EXIT_FAILURE);
    }
    
    // print perofrmance times
    fprintf(stdout, "\npeformance summary\n");
    fprintf(stdout, "read:              %f\n", time_read);
    fprintf(stdout, "conv tensor:       %f\n", time_tens);
    fprintf(stdout, "trim:              %f\n", time_trim);
    fprintf(stdout, "scale:             %f\n", time_scale);
    fprintf(stdout, "chunk:             %f\n", time_chunk);
    fprintf(stdout, "basecall:          %f\n", time_basecall);
    fprintf(stdout, "decode:            %f\n", time_decode);
    fprintf(stdout, "stitch:            %f\n", time_stitch);
    fprintf(stdout, "write:             %f\n", time_write);
    fprintf(stdout, "samples/ps:        %f\n", n_samples / total_time);

    fprintf(stderr,"\n");

    slow5_rec_free(rec);
    slow5_close(sp);

    return 0;
}
