/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/kallsyms.h>
#include <linux/smp.h>
#include <linux/uaccess.h>
#include <linux/cpu.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/kexec.h>
#include <asm/cputype.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/hardware.h>


extern void relocate_new_kernel(void);
extern const unsigned int relocate_new_kernel_size;

extern unsigned long kexec_start_address;
extern unsigned long kexec_indirection_page;
extern unsigned long kexec_mach_type;
extern unsigned long kexec_boot_atags;

void kexec_cpu_proc_fin(void);
void kexec_cpu_reset(void);

extern void call_with_stack(void (*fn)(void *), void *arg, void *sp);
typedef void (*phys_reset_t)(void);

static DEFINE_SPINLOCK(main_lock);
void __iomem *myL2CacheBase = NULL;
/*
static uint32_t kexec_l2x0_way_mask = (1 << 16 ) - 1;  /* Bitmask of active PL310 ways (on RAZR..) */
*/
void flushcachesinit(void);
void setwayflush(void);

void pulse(void)
{
  asm volatile(
    "ldr  r3, =0x4a310000    \n\t"
    "mov  r4, #0x10000000    \n\t"
    "str  r4, [r3, #0x190]  \n\t"
    "str  r4, [r3, #0x194]  \n\t"
    ::: "r3", "r4"
  );
}

/*
 * A temporary stack to use for CPU reset. This is static so that we
 * don't clobber it with the identity mapping. When running with this
 * stack, any references to the current task *will not work* so you
 * should really do as little as possible before jumping to your reset
 * code.
 */
static u32 soft_restart_stack[256];

static void kexec_info(struct kimage *image)
{
  int i;
  printk("kexec information\n");
  for (i = 0; i < image->nr_segments; i++) {
          printk("  segment[%d]: 0x%08x - 0x%08x (0x%08x)\n",
           i,
           (unsigned int)image->segment[i].mem,
           (unsigned int)image->segment[i].mem +
             image->segment[i].memsz,
           (unsigned int)image->segment[i].memsz);
  }
  printk("  start     : 0x%08x\n", (unsigned int)image->start);
  printk("  atags     : 0x%08x\n", (unsigned int)image->start - KEXEC_ARM_ZIMAGE_OFFSET + KEXEC_ARM_ATAGS_OFFSET);
  printk("machine_arch_type: %04x\n", machine_arch_type);
}





void (*cpu_reset_phys)(unsigned long int dest);

static atomic_t waiting_for_crash_ipi;
 /*
 * Provide a dummy crash_notes definition while crash dump arrives to arm.
 * This prevents breakage of crash_notes attribute in kernel/ksysfs.c.
 */
int machine_kexec_prepare(struct kimage *image)
{
	kexec_info(image);
	return 0;
};

void machine_kexec_cleanup(struct kimage *image)
{
};

EXPORT_SYMBOL(machine_kexec_cleanup);

void machine_crash_nonpanic_core(void *unused)
{
	struct pt_regs regs;

	crash_setup_regs(&regs, NULL);
	printk(KERN_DEBUG "CPU %u will stop doing anything useful since another CPU has crashed\n",
	       smp_processor_id());
	crash_save_cpu(&regs, smp_processor_id());
	flush_cache_all();

	atomic_dec(&waiting_for_crash_ipi);
	while (1)
		cpu_relax();
}

void machine_crash_shutdown(struct pt_regs *regs)
{
	unsigned long msecs;

	local_irq_disable();

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);
	smp_call_function(machine_crash_nonpanic_core, NULL, false);
	msecs = 1000; /* Wait at most a second for the other cpus to stop */
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && msecs) {
		mdelay(1);
		msecs--;
	}
	if (atomic_read(&waiting_for_crash_ipi) > 0)
		printk(KERN_WARNING "Non-crashing CPUs did not react to IPI\n");

	crash_save_cpu(regs, smp_processor_id());

	printk(KERN_INFO "Loading crashdump kernel...\n");
}

/*
 * Function pointer to optional machine-specific reinitialization
 */
void (*kexec_reinit)(void);

void machine_kexec(struct kimage *image)
{
	unsigned long page_list;
	unsigned long reboot_code_buffer_phys;
	void *reboot_code_buffer;

	arch_kexec();

	page_list = image->head & PAGE_MASK;

	/* we need both effective and real address here */
	reboot_code_buffer_phys =
	    page_to_pfn(image->control_code_page) << PAGE_SHIFT;
	reboot_code_buffer = page_address(image->control_code_page);

	/* Prepare parameters for reboot_code_buffer*/
	kexec_start_address = image->start;
	kexec_indirection_page = page_list;
	kexec_mach_type = machine_arch_type;
	kexec_boot_atags = image->start - KEXEC_ARM_ZIMAGE_OFFSET + KEXEC_ARM_ATAGS_OFFSET;

	/* copy our kernel relocation code to the control code page */
	memcpy(reboot_code_buffer,
	       relocate_new_kernel, relocate_new_kernel_size);


	flush_icache_range((unsigned long) reboot_code_buffer,
			   (unsigned long) reboot_code_buffer + KEXEC_CONTROL_PAGE_SIZE);
	printk(KERN_INFO "Bye!\n");

	if (kexec_reinit)
		kexec_reinit();
	local_irq_disable();
	local_fiq_disable();
	setup_mm_for_reboot(0); /* mode is not used, so just pass 0*/
	flush_cache_all();
	outer_flush_all();
	outer_disable();
	cpu_proc_fin();
	outer_inv_all();
	flush_cache_all();
	virt_to_phys(cpu_reset);
	cpu_reset_phys(reboot_code_buffer_phys);
/*
	__virt_to_phys(cpu_reset)(reboot_code_buffer_phys);
*/
/*-	cpu_reset(reboot_code_buffer_phys); */
/*
* cpu_reset disables the MMU, so branch to its (1-to-1 mapped)
* physical address not its virtual one.
*/
}
static int __init arm_kexec_init(void)
{
  void (*set_cpu_online_ptr)(unsigned int cpu, bool online) = (void *)kallsyms_lookup_name("set_cpu_online");
  void (*set_cpu_present_ptr)(unsigned int cpu, bool present) = (void *)kallsyms_lookup_name("set_cpu_present");
  void (*set_cpu_possible_ptr)(unsigned int cpu, bool possible) = (void *)kallsyms_lookup_name("set_cpu_possible");
  
  int (*disable_nonboot_cpus)(void) = (void *)kallsyms_lookup_name("disable_nonboot_cpus");
  int nbcval = 0;

  nbcval = disable_nonboot_cpus();
  if (nbcval < 0)
    printk(KERN_INFO "!!!WARNING!!! disable_nonboot_cpus have FAILED!\n \
          Continuing to boot anyway: something can go wrong!\n");

  set_cpu_online_ptr(1, false);
  set_cpu_present_ptr(1, false);
  set_cpu_possible_ptr(1, false);

  return 0;
}

static void __exit arm_kexec_exit(void)
{
}

module_init(arm_kexec_init);
module_exit(arm_kexec_exit);

EXPORT_SYMBOL(machine_kexec);
MODULE_LICENSE("GPL");

