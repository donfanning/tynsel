#include <stdlib.h>
#include <fftw3.h>
#include <math.h>
#include <float.h>
#include <string.h>

#include "audio.h"

#define BUFFER_SIZE 16384 * 3

#define WINDOW_SIZE 16000

#define countof(X) (sizeof (X) / sizeof (X)[0])

int read_file(struct audio_state *a, const char *filename, size_t size, double input[size]);

static int do_fft(int sign, size_t in_size, fftw_complex input[in_size], fftw_complex output[])
{
    // XXX should create plan before initialising data ; this is absolutely
    // necessary if we switch to FFTW_MEASURE
    fftw_plan plan = fftw_plan_dft_1d(in_size, input, output, sign, FFTW_ESTIMATE);
    fftw_execute(plan);
    fftw_destroy_plan(plan);

    return 0;
}

static void complexify(size_t size, double input[size], fftw_complex output[size])
{
    for (size_t i = 0; i < size; i++) {
        output[i][0] = input[i];
        output[i][1] = 0.;
    }
}

static void realify(size_t size, fftw_complex input[size], double output[size])
{
    for (size_t i = 0; i < size; i++) {
        output[i] = sqrt(pow(input[i][0], 2) + pow(input[i][1], 2));
    }
}

static void conjugate(size_t size, fftw_complex input[size], fftw_complex output[size])
{
    for (size_t i = 0; i < size; i++) {
        output[i][0] =  input[i][0];
        output[i][1] = -input[i][1];
    }
}

static void multiply(size_t size, fftw_complex C[size], fftw_complex A[size], fftw_complex B[size])
{
    for (size_t i = 0; i < size; i++) {
        // (a + bi) * (c + di)
        double a = A[i][0],
               b = A[i][1],
               c = B[i][0],
               d = B[i][1];
        C[i][0] = a * c - b * d;
        C[i][1] = b * c + a * d;
    }
}

static size_t get_maxes(size_t size, double ins[size], size_t *count,
        double outs[*count], size_t indices[*count])
{
    outs[0] = -DBL_MAX;
    size_t im = 0;
    size_t c = 0;
    for (size_t i = 0; i < size; i++)
        if (ins[i] > outs[0]) {
            memmove(&outs[1], &outs[0], (*count - 1) * sizeof *outs);
            memmove(&indices[1], &indices[0], (*count - 1) * sizeof *indices);
            if (c < *count)
                c++;
            outs[0] = ins[i];
            indices[0] = i;
        }

    *count = c;

    return im;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        abort();

    char *ref_file = argv[1],
         *tst_file = argv[2];

    size_t ref_size = 0,
           tst_size = 0;

    struct audio_state as = { .sample_offset = 0 };

    double (*buffer)[BUFFER_SIZE] = malloc(sizeof *buffer);

    fftw_complex (*ref_data)[BUFFER_SIZE] = fftw_malloc(sizeof *ref_data);
    ref_size = read_file(&as, ref_file, countof(*buffer), *buffer);
    complexify(ref_size, *buffer, *ref_data);

    fftw_complex (*tst_data)[BUFFER_SIZE] = fftw_malloc(sizeof *tst_data);
    tst_size = read_file(&as, tst_file, countof(*buffer), *buffer);
    complexify(tst_size, *buffer, *tst_data);

    free(*buffer);

    fftw_complex (*ref_fft)[WINDOW_SIZE] = fftw_malloc(sizeof *ref_fft),
                 (*tst_fft)[WINDOW_SIZE] = fftw_malloc(sizeof *tst_fft),
                 (*cnj_fft)[WINDOW_SIZE] = fftw_malloc(sizeof *cnj_fft),
                 (*multed )[WINDOW_SIZE] = fftw_malloc(sizeof *multed );

    do_fft(FFTW_FORWARD, WINDOW_SIZE, *ref_data, *ref_fft);
    do_fft(FFTW_FORWARD, WINDOW_SIZE, *tst_data, *tst_fft);

    free(*ref_data);
    free(*tst_data);

    conjugate(WINDOW_SIZE, *ref_fft, *cnj_fft);

    multiply(WINDOW_SIZE, *multed, *cnj_fft, *tst_fft);
    fftw_free(*ref_fft);
    fftw_free(*tst_fft);
    fftw_free(*cnj_fft);

    fftw_complex (*reversed)[WINDOW_SIZE] = fftw_malloc(sizeof *reversed);
    do_fft(FFTW_BACKWARD, WINDOW_SIZE, *multed, *reversed);
    double (*result)[WINDOW_SIZE] = malloc(sizeof *result);
    realify(WINDOW_SIZE, *reversed, *result);

    size_t max_count = 10;
    double maxes[max_count];
    size_t imaxes[max_count];
    get_maxes(tst_size, *result, &max_count, maxes, imaxes);
    printf("0result[%5zd] = %e\n", 0            , (*result)[0            ]);
    for (size_t i = 0; i < max_count; i++)
        printf(" result[%5zd] = %e\n", imaxes[i], (*result)[imaxes[i]]);
    free(*result);

    return 0;
}
