#include <stdbool.h>
#include "threads/interrupt.h"

void page_fault_handler (struct intr_frame *f, bool not_present, bool write, bool user, void *fault_addr);
void bad_exit (struct intr_frame *f);
