#include "decode.h"
#include "common.h"

#include <math.h>
#include <fftw3.h>
#include <float.h>

#define ROUND_FACTOR(X,By) (((X) + (By) - 1) / (By))

#define LOWEND          (s->freqs[0][0] - PERBIN)
#define HIGHEND         (s->freqs[1][1] + PERBIN)
#define PERBIN          ((double)s->sample_rate / s->fft_size)
#define ALL_BITS        (s->start_bits + s->data_bits + s->parity_bits + s->stop_bits)

static size_t get_max_magnitude(struct decode_state *s, fftw_complex *fft_result)
{
    size_t maxi = 0;
    double max = -1;
    for (size_t i = 0; i < s->fft_size; i++) {
        double mag = sqrt(pow(fft_result[i][0],2) + pow(fft_result[i][1],2));
        if (s->verbosity > 3) {
            printf("fft_result[%zd] = { %2.2f, %2.2f }\n", i, fft_result[i][0], fft_result[i][1]);
            printf("magnitude[%zd] = { %6.2f }\n", i, mag);
        }
        if (mag > max && (LOWEND / PERBIN) <= i && i <= (HIGHEND / PERBIN)) {
            max = mag;
            maxi = i;
        }
    }

    return maxi;
}

static void get_nearest_freq(struct decode_state *s, double freq, int *ch, int *f)
{
    double min = DBL_MAX;

    if (s->verbosity)
        printf("midpoint frequency is %4.0f\n", freq);

    for (unsigned chan = 0; chan < 2; chan++) {
        for (unsigned i = 0; i < 2; i++) {
            double t = fabs(freq - s->freqs[chan][i]);
            if (t < min) {
                min = t;
                *ch = chan;
                *f = i;
            }
        }
    }
}

int decode_bit(struct decode_state *s, fftw_complex *fft_result, int *channel, int *bit)
{
    size_t maxi = get_max_magnitude(s, fft_result);

    if (s->verbosity) {
        printf("bucket with greatest magnitude was %zd, which corresponds to frequency range [%4.0f, %4.0f)\n",
                maxi, PERBIN * maxi, PERBIN * (maxi + 1));
    }

    double freq = maxi * PERBIN + (PERBIN / 2.);
    get_nearest_freq(s, freq, channel, bit);

    return 0;
}

int decode_byte(struct decode_state *s, size_t size, double input[size], int output[ (size_t)(size / SAMPLES_PER_BIT(s) / ALL_BITS) ], double *offset)
{
    fftw_complex *data       = fftw_malloc(s->fft_size * sizeof *data);
    fftw_complex *fft_result = fftw_malloc(s->fft_size * sizeof *fft_result);

    int biti = 0;
    for (double dbb = *offset; dbb < size + *offset; dbb += SAMPLES_PER_BIT(s), biti++) {
        int word = biti / ALL_BITS;
        int wordbit = biti % ALL_BITS;
        if (wordbit == 0)
            output[word] = 0;

        fftw_plan plan_forward = fftw_plan_dft_1d(s->fft_size, data, fft_result, FFTW_FORWARD, FFTW_ESTIMATE);

        size_t bit_base = dbb;
        *offset += dbb - bit_base;

        for (size_t i = 0; i < s->fft_size; i++) {
            data[i][0] = i < SAMPLES_PER_BIT(s) ? input[bit_base + i] : 0.;
            data[i][1] = 0.;
        }

        fftw_execute(plan_forward);

        int channel, bit;
        decode_bit(s, fft_result, &channel, &bit);
        output[word] |= bit << wordbit;

        if (s->verbosity > 2) {
            printf("Guess : channel %d bit %d\n", channel, bit);
            int width = ROUND_FACTOR(ALL_BITS, 4);
            printf("output[%d] = 0x%0*x\n", word, width, output[word]);
        }

        fftw_destroy_plan(plan_forward);
    }

    fftw_free(data);
    fftw_free(fft_result);

    return 0;
}

int decode_data(struct decode_state *s, size_t count, double input[count])
{
    int output[ (size_t)(count / SAMPLES_PER_BIT(s) / ALL_BITS) ];

    // TODO merge `offset` and `s->sample_offset`
    double offset = 0.;
    decode_byte(s, count - s->sample_offset, input, output, &offset);
    for (size_t i = 0; i < countof(output); i++) {
        if (output[i] & ((1 << s->start_bits) - 1))
            fprintf(stderr, "Start bit%s %s not zero\n",
                    s->start_bits > 1 ? "s" : "", s->start_bits > 1 ? "were" : "was");
        if (output[i] >> (ALL_BITS - s->stop_bits) != (1 << s->stop_bits) - 1)
            fprintf(stderr, "Stop bits were not one\n");
        int width = ROUND_FACTOR(s->data_bits, 4);
        printf("output[%zd] = 0x%0*x\n", i, width, (output[i] >> s->start_bits) & ((1u << s->data_bits) - 1));
    }

    return 0;
}

void decode_cleanup(void)
{
    fftw_cleanup();
}

