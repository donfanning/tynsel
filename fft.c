#include "common.h"
#include "decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <math.h>

#include <sndfile.h>

#define BUFFER_SIZE 16384 * 16

static const double bell103_freqs[2][2] = {
    { 1070., 1270. },
    { 2025., 2225. },
};

static int read_file(struct decode_state *s, const char *filename, size_t size, double input[size])
{
    size_t index = 0;
    SNDFILE *sf = NULL;
    {
        SF_INFO sinfo = { .format = 0 };
        sf = sf_open(filename, SFM_READ, &sinfo);
        if (!sf) {
            fprintf(stderr, "Failed to open `%s' : %s\n", filename, strerror(errno));
            exit(EXIT_FAILURE);
        }

        // getting the sample rate from the file means right now the `-s' option on
        // the command line has no effect. In the future it might be removed, or it
        // might be necessary when the audio input has no accompanying sample-rate
        // information.
        s->sample_rate = sinfo.samplerate;
    }

    {
        sf_count_t count = 0;
        do {
            count = sf_read_double(sf, &input[(size_t)SAMPLES_PER_BIT(s) + index++], 1);
        } while (count && index < BUFFER_SIZE);

        if (index >= BUFFER_SIZE)
            fprintf(stderr, "Warning, ran out of buffer space before reaching end of file\n");

        sf_close(sf);
    }

    return index;
}

static int parse_opts(struct decode_state *s, int argc, char *argv[], const char **filename)
{
    int ch;
    while ((ch = getopt(argc, argv, "S:T:P:D:s:O:v")) != -1) {
        switch (ch) {
            case 'S': s->start_bits    = strtol(optarg, NULL, 0); break;
            case 'T': s->stop_bits     = strtol(optarg, NULL, 0); break;
            case 'P': s->parity_bits   = strtol(optarg, NULL, 0); break;
            case 'D': s->data_bits     = strtol(optarg, NULL, 0); break;
            case 's': s->sample_rate   = strtol(optarg, NULL, 0); break;
            case 'O': s->sample_offset = strtod(optarg, NULL);    break;
            case 'v': s->verbosity++; break;
            default: fprintf(stderr, "args error before argument index %d\n", optind); return -1;
        }
    }

    if (fabs(s->sample_offset) > SAMPLES_PER_BIT(s)) {
        fprintf(stderr, "sample offset (%f) > samples per bit (%4.1f)\n",
                s->sample_offset, SAMPLES_PER_BIT(s));
        return -1;
    }

    if (optind < argc)
        *filename = argv[optind];

    return 0;
}

int main(int argc, char* argv[])
{
    struct decode_state _s = {
        .sample_rate = 44100,
        .baud_rate   = 300, // TODO make baud_rate configurable
        .fft_size    = 512,
        .start_bits  = 1,
        .data_bits   = 8,
        .parity_bits = 0,
        .stop_bits   = 2,
        .verbosity   = 0,
        .freqs       = bell103_freqs,
    }, *s = &_s;

    const char *filename = NULL;
    parse_opts(s, argc, argv, &filename);
    if (!filename) {
        fprintf(stderr, "No files specified to process\n");
        return -1;
    }

    double *_input = calloc((size_t)SAMPLES_PER_BIT(s) * 2 + BUFFER_SIZE, sizeof *_input);
    double *input = &_input[(size_t)SAMPLES_PER_BIT(s) + (size_t)s->sample_offset];
    size_t count = read_file(s, argv[optind], sizeof _input, _input);

    if (s->verbosity) {
        printf("read %zd items\n", count);
        printf("sample rate is %4d Hz\n", s->sample_rate);
        printf("baud rate is %4d\n", s->baud_rate);
        printf("samples per bit is %4.0f\n", SAMPLES_PER_BIT(s));
    }

    process_data(s, count, input);

    free(_input);
    decode_cleanup();

    return 0;
}

