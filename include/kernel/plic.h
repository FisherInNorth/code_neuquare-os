#ifndef __PLIC_H__
#define __PLIC_H__

void plicinit(void);
void plicinithart(void);
int plic_claim(void);
void plic_complete(int);

#endif // __PLIC_H__