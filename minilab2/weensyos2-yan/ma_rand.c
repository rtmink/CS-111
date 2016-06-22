#include "ma_rand.h"
//#include "types.h"

// Uses a linear feedback shift register
// Found at http://stackoverflow.com/a/7603688

unsigned short lfsr = 0xACE1u;
unsigned bit;

unsigned ma_rand()
{
    bit  = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5) ) & 1;
    return lfsr =  (lfsr >> 1) | (bit << 15);
}