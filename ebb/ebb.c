/* Event-Based Branch Facility API.  API implementation.
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
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>

#include "config.h"
#include <paf/ebb.h>
#include "ebb-priv.h"
#include "ebb-callback.h"
#include "ebb-hwcap.h"


/* Per-thread EBB handler information if TCB fields are not available.  */
__thread
attribute_initial_exec
attribute_hidden
struct ebb_thread_info_t __paf_ebb_thread_info = { 0, NULL };

/* Helper function to start the Linux perf/EBB.  */

static int
paf_ebb_pmu_event_init (uint64_t raw_event, int group, pid_t pid, int cpu)
{
  struct perf_event_attr pe;
  int fd;
  uint64_t count;

  memset (&pe, 0, sizeof (struct perf_event_attr));
  pe.type = PERF_TYPE_RAW;
  pe.size = sizeof (struct perf_event_attr);
  /* Bit 63 from perf_event_attr::config indicate if it is an EBB setup. */
  pe.config = raw_event | UINT64_C (0x8000000000000000);
  /* EBB setup has strict flags configuration: only the group leader
   * (group == -1) can have the pinned and exclusive bit set.  */
  pe.pinned = (group == -1) ? 1 : 0;
  pe.exclusive = (group == -1) ? 1 : 0;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;
  pe.exclude_idle = 1;

  /* It also need to be attached to a task:  */

  fd = syscall (__NR_perf_event_open, &pe, pid, cpu, group, 0);
  if (fd == -1)
    return -1;

  if (ioctl (fd, PERF_EVENT_IOC_ENABLE, 0) != 0)
    {
      close (fd);
      return -1;
    }

  if (read (fd, &count, sizeof (count)) == EOF)
    {
      close (fd);
      return -1;
    }

  return fd;
}
int
paf_ebb_pmu_init (uint64_t raw_event, int group)
{
  return (paf_ebb_pmu_event_init(raw_event, group, 0, -1));
}
int
paf_ebb_pmu_init_with_pid (uint64_t raw_event, int group, pid_t pid)
{
  return (paf_ebb_pmu_event_init(raw_event, group, pid, -1));
}

int
paf_ebb_pmu_init_with_cpu (uint64_t raw_event, int group, int cpu)
{
  return (paf_ebb_pmu_event_init(raw_event, group, 0, cpu));
}
int
paf_ebb_event_close (int fd)
{
  if (ioctl (fd, PERF_EVENT_IOC_DISABLE) != 0)
    return -1;
  return close (fd);
}

void
paf_ebb_pmu_reset (void)
{
  uint32_t sample_period = __paf_ebb_get_thread_sample_period ();
  reset_mmcr0 ();
  reset_pmcs (sample_period);
}

void
paf_ebb_pmu_set_period (uint32_t sample_period)
{
  __paf_ebb_set_thread_sample_period (sample_period);
}

/* Return the internal EBB callback function. Since EBB it will just
 * execute the first instruction from the address set into EBBHR, its
 * value should be the functions text, not its ODP.  */
static inline uintptr_t
__ebb_callback_handler_addr (paf_ebb_callback_type_t type)
{
  void (*callback) (void);
  if (type == PAF_EBB_CALLBACK_GPR_SAVE)
    callback = __paf_ebb_callback_handler_gpr;
  else if (type == PAF_EBB_CALLBACK_FPR_SAVE)
    callback = __paf_ebb_callback_handler_fpr;
  else if (type == PAF_EBB_CALLBACK_VR_SAVE)
    callback = __paf_ebb_callback_handler_vr;
  else				// (type == PAF_EBB_CALLBACK_VSR_SAVE)
    callback = __paf_ebb_callback_handler_vsr;

#ifdef __powerpc64__
  struct odp_entry_t
  {
    uintptr_t addr;
    uintptr_t toc;
  } *odp_entry = (struct odp_entry_t *) (callback);
  return odp_entry->addr;
#else
  return (uintptr_t) callback;
#endif
}

/* Return the thread handler registered with a previous
 * paf_ebb_register_handler or NULL if it is not set yet.
 */
ebbhandler_t
paf_ebb_handler (void)
{
  ebbhandler_t ret;
  if (!(__paf_ebb_hwcap & PAF_EBB_FEATURE_HAS_EBB))
    {
      errno = ENOSYS;
      return EBB_REG_ERR;
    }
  ret = __paf_ebb_get_thread_handler ();
  return ret == NULL ? EBB_REG_ERR : ret;
}

ebbhandler_t
paf_ebb_register_handler (ebbhandler_t handler, void *context,
			  paf_ebb_callback_type_t type, int flags)
{
  uintptr_t handlerfp;

  if (!(__paf_ebb_hwcap & PAF_EBB_FEATURE_HAS_EBB))
    {
      errno = ENOSYS;
      return EBB_REG_ERR;
    }
  if (handler == NULL)
    return EBB_REG_ERR;


  __paf_ebb_set_thread_handler (handler);
  __paf_ebb_set_thread_context (context);
  __paf_ebb_set_thread_flags (flags);

  handlerfp = __ebb_callback_handler_addr (type);
  mtspr (EBBHR, handlerfp);

  return handler;
}

int
paf_ebb_enable_branches (void)
{
  if (!(__paf_ebb_hwcap & PAF_EBB_FEATURE_HAS_EBB))
    {
      errno = ENOSYS;
      return -1;
    }

  /* Enable PMU Event-Based exception (PME - bit 31).  */
  PAF_EBB_ENABLE();
  return 0;
}

int
paf_ebb_disable_branches (void)
{
  if (!(__paf_ebb_hwcap & PAF_EBB_FEATURE_HAS_EBB))
    {
      errno = ENOSYS;
      return -1;
    }

  /* Disable PMU Event-Based exception (PME - bit 31).  */
  PAF_EBB_DISABLE();
  return 0;
}
