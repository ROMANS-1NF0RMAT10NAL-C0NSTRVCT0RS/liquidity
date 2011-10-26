#ifndef __DIE_H
#define __DIE_H

#ifdef __cplusplus
extern "C" {
#endif

void die(const char * const, unsigned int);

#ifdef __cplusplus
}
#endif

#define DIE die(__FILE__,__LINE__);

#endif
