#include <sys/time.h>
#include <limits.h>

#include "gale/misc.h"

struct gale_time gale_time_zero(void) {
	struct gale_time time;
	time.sec_high = -2147483647 - 1;
	time.sec_low = 0;
	time.frac_high = 0;
	time.frac_low = 0;
	return time;
}

struct gale_time gale_time_forever(void) {
	struct gale_time time;
	time.sec_high = 2147483647;
	time.sec_low = 4294967295U;
	time.frac_high = 4294967295U;
	time.frac_low = 4294967295U;
	return time;
}

struct gale_time gale_time_now(void) {
	static struct gale_time last = { 0, 0, 0, 0 };
	struct gale_time time;
	struct timeval tv;
	gettimeofday(&tv,NULL);
	gale_time_from(&time,&tv);

	if (gale_time_compare(last,time) < 0) 
		last = time;
	else {
		/* Ensure monotonicity. */
		++last.frac_low;
		time = last;
	}

	return time;
}

struct gale_time gale_time_seconds(int sec) {
	struct gale_time time;
	if (sec < 0) {
		time.sec_high = -1;
		time.sec_low = sec /* + 2^32 */;
	} else {
		time.sec_high = 0;
		time.sec_low = sec;
	}
	return time;
}

int gale_time_compare(struct gale_time one,struct gale_time two) {
	if (one.sec_high < two.sec_high) return -1;
	if (one.sec_high > two.sec_high) return 1;
	if (one.sec_low < two.sec_low) return -1;
	if (one.sec_low > two.sec_low) return 1;
	if (one.frac_high < two.frac_high) return -1;
	if (one.frac_high > two.frac_high) return 1;
	if (one.frac_low < two.frac_low) return -1;
	if (one.frac_low > two.frac_low) return 1;
	return 0;
}

static u32 subtract(u32 one,u32 two,int *borrow) {
	u32 result = one - two - *borrow;
	*borrow = (result > one || (two && result == one));
	return result;
}

struct gale_time gale_time_diff(struct gale_time one,struct gale_time two) {
	struct gale_time time;
	int borrow = 0;
	time.frac_low = subtract(one.frac_low,two.frac_low,&borrow);
	time.frac_high = subtract(one.frac_high,two.frac_high,&borrow);
	time.sec_low = subtract(one.sec_low,two.sec_low,&borrow);
	time.sec_high = subtract(one.sec_high,two.sec_high,&borrow);
	return time;
}

void gale_time_to(struct timeval *tv,struct gale_time time) {
	if (time.sec_high != 0) {
		gale_alert(GALE_WARNING,"time overflow converting to Unix",0);
		tv->tv_sec = UINT_MAX;
		tv->tv_usec = 0;
	}
	tv->tv_sec = time.sec_low;
	tv->tv_usec = time.frac_high / 16384 * 15625 / 4096;
}

void gale_time_from(struct gale_time *time,struct timeval *tv) {
	time->sec_high = 0;
	time->sec_low = tv->tv_sec;
	time->frac_high = tv->tv_usec * 4096 / 15625 * 16384;
	time->frac_low = 0;
}

void gale_pack_time(struct gale_data *data,struct gale_time time) {
	gale_pack_u32(data,time.sec_high);
	gale_pack_u32(data,time.sec_low);
	gale_pack_u32(data,time.frac_high);
	gale_pack_u32(data,time.frac_low);
}

int gale_unpack_time(struct gale_data *data,struct gale_time *time) {
	return gale_unpack_u32(data,(u32 *) &time->sec_high)
	    && gale_unpack_u32(data,&time->sec_low)
	    && gale_unpack_u32(data,&time->frac_high)
	    && gale_unpack_u32(data,&time->frac_low);
}
