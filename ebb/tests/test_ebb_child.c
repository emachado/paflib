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
 *
 * Contributors:
 *     IBM Corporation, Adhemerval Zanella - Initial implementation.
 */

#include <stdio.h>
#include <errno.h>
#include <paf/ebb.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include "test_ebb_common.h"
static volatile int ebb_handler_triggered = 0;

#define TEST_LOOP_COUNT 2

/*
 * * Tests we can setup an EBB on our child. The child expects this and enables
 * * EBBs, which are then delivered to the child, even though the event is
 * * created by the parent.
 * */

static void attribute_noinline
ebb_handler_test (void *context)
{
  int *trigger = (int *) (context);
  printf ("%s: ebb_handler_triggered address = %p\n", __FUNCTION__, trigger);
  *trigger += 1;
}

int
child ()
{
  ebbhandler_t handler;

  ebb_handler_triggered = 0;
  printf ("Setting Handler on child\n");
  /* Setup our EBB handler, before the EBB event is created */
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

  sleep (3);
  printf ("Enabling EBB on child\n");
  paf_ebb_enable_branches ();

  paf_ebb_pmu_reset ();
  while (ebb_handler_triggered != TEST_LOOP_COUNT)
    {
      if (ebb_check_mmcr0 ())
	return 1;
    }

  paf_ebb_disable_branches ();

  return 0;
}

int
wait_for_child (pid_t child_pid)
{
  int rc;

  if (waitpid (child_pid, &rc, 0) == -1)
    {
      perror ("waitpid");
      return 1;
    }

  if (WIFEXITED (rc))
    rc = WEXITSTATUS (rc);
  else
    rc = 1;			/* Signal or other */

  return rc;
}

/* Tests we can setup an EBB on child.*/
int
ebb_on_child (void)
{
  pid_t pid;
  int ebbfd;

  pid = fork ();
  if (pid == 0)
    {
      child ();
      exit (0);
    }
  sleep (2);

  printf ("Setting EBB on child\n");
  ebbfd = paf_ebb_pmu_init_with_pid (0x1001e, -1, pid);
  paf_ebb_event_read (ebbfd);
  if (ebbfd == -1)
    {
      printf ("Error: paf_ebb_pmu_init_with_pid () failed " "(errno = %i)\n", errno);
      return -1;
    }

  if (wait_for_child (pid))
    {
      printf ("Error: waiting for child");
      return 1;
    }
  paf_ebb_event_close (ebbfd);

  return 0;
}

int
main (void)
{
  int ret = 0;

  ret += ebb_on_child ();

  return ret;
}
