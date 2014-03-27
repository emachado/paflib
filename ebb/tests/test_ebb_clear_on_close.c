/* Event-Based Branch Facility Tests.
 *
 * Copyright IBM Corp. 2013
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <errno.h>
#include <paf/ebb.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>
#include "test_ebb_common.h"

static volatile int ebb_handler_triggered = 0;
static jmp_buf setjmp_env;

#define TEST_LOOP_COUNT 2

static void attribute_noinline
ebb_handler_test (void *context)
{
  int *trigger = (int *) (context);
  printf ("%s: ebb_handler_triggered address = %p\n", __FUNCTION__, trigger);
  *trigger += 1;
}


static void
sigill_handler (int signal)
{
  printf ("Took sigill as expected\n");
  longjmp (setjmp_env, 1);
}

static struct sigaction sigill_action = {
  .sa_handler = sigill_handler,
};

int
catch_sigill (void (*func) (void))
{
  if (sigaction (SIGILL, &sigill_action, NULL))
    {
      perror ("sigaction");
      return 1;
    }

  if (setjmp (setjmp_env) == 0)
    {
      func ();
    }

  return 0;
}

void
write_pmc1 (void)
{
  mtspr (PMC1, 0);
}

/*
* Test that closing the EBB event clears MMCR0_PMCC, preventing further access
* by userspace to the PMU hardware.
*/
int
test_ebb_clear_on_close (void)
{
  int ebbfd;
  ebbhandler_t handler;

  ebbfd = paf_ebb_pmu_init (0x1001e, -1);

  if (ebbfd == -1)
    {
      printf ("Error: paf_ebb_init_pmu () failed " "(errno = %i)\n", errno);
      return -1;
    }

  ebb_handler_triggered = 0;

  handler = paf_ebb_register_handler (ebb_handler_test,
				      (void *) &ebb_handler_triggered,
				      PAF_EBB_CALLBACK_GPR_SAVE,
				      PAF_EBB_FLAGS_RESET_PMU);
  if (handler != ebb_handler_test)
    {
      printf ("Error: paf_ebb_register_handler \
              (ebb_handler_test) != handler\n");
      return -1;
    }

  paf_ebb_enable_branches ();

  paf_ebb_pmu_reset ();

  while (ebb_handler_triggered != TEST_LOOP_COUNT)
    {
      if (ebb_check_mmcr0 ())
	return 1;
    }

  paf_ebb_disable_branches ();

  paf_ebb_event_close (ebbfd);

  if (catch_sigill (write_pmc1))
    {
      printf ("Error: catch_sigill () failed " "(errno = %i)\n", errno);
      return -1;
    }
  return 0;
}

int
main (void)
{
  int ret = 0;

  ret += test_ebb_clear_on_close ();

  return ret;
}
