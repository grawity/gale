/* header.h -- puff header management */

#ifndef HEADER_H
#define HEADER_H

/* Parse a Gale message body for headers.

   next    Pointer to a pointer, initially set to the beginning of the data
   key     Pointer to a pointer, initially uninitialized
   data    Pointer to a pointer, initially uninitialized
   end     Pointer to the end of the message (data + data_size)

   After calling, "key" will point to the (NUL-terminated) first header name
   ("From") and "data" will point to the (NUL-terminated) contents of the
   header.  These point into the message data itself, which is munched as a
   side effect (NULs are introduced).

   "next" is advanced to point to the remainder of the message after the first
   header, so you can loop back and call the routine again to get the next
   header.

   The routine returns zero when no more headers are found, at which point
   "next" points to the message body. */

int parse_header(char **next,char **key,char **data,char *end);

#endif
