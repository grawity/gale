#ifndef MSG_OLD_H
#define MSG_OLD_H

#include "gale/core.h"

struct gale_fragment **unpack_old_message(struct gale_data);
struct gale_data pack_old_message(struct gale_fragment **);

#endif
