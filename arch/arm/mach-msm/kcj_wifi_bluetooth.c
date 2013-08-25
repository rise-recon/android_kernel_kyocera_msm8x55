/*
 * This software is contributed or developed by KYOCERA Corporation.
 * (C) 2012 KYOCERA Corporation  
*/
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <linux/mfd/pmic8058.h>
#include <mach/msm_iomap.h>
#include <mach/pmic.h>
#include <mach/rpc_pmapp.h>
#include <mach/vreg.h>
#include "pm.h"
#include "spm.h"

#include <linux/proc_fs.h>
#include <asm/uaccess.h>

/* For msm_batt_oem_update_dev_info_wlan function prototype. */
#include <mach/kcj_dev_info.h>

#include "proc_comm.h"

#include <mach/kcj_wifi_bluetooth.h>

#define PM8058_GPIO_SLEEP_CLK 37 /* PMIC GPIO 38 */

/* Macros assume PMIC GPIOs start at 0 */
#define PM8058_GPIO_PM_TO_SYS(pm_gpio)     (pm_gpio + NR_GPIO_IRQS)
#define PM8058_GPIO_SYS_TO_PM(sys_gpio)    (sys_gpio - NR_GPIO_IRQS)

static int wifi_sleep_clock_initialize( void );

static int kcj_wifi_power_initialize_read(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
static int kcj_wifi_power_initialize_write(struct file *file, const char *buffer,
					unsigned long count, void *data);
static int kcj_wifi_debug_read(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
static int kcj_wifi_debug_write(struct file *file, const char *buffer,
					unsigned long count, void *data);

static int bluepower_read_proc_bt_addr(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
static int bluepower_read_proc_wlan_mac_addr(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
static int read_proc_product_line(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
static int bluepower_read_proc_wlan_mac_addr(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
static int bluepower_read_proc_wlan_mac_addr_qcom(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
static int bluepower_read_proc_wlan_mac_addr_common(char *page, char **start, off_t offset,
					int count, int *eof, void *data, int format_mode);

static int check_product_line(void);

/* Virtual file system read handler(/proc/kyocera_wifi/wifi_state) be add. */
static int kcj_wifi_state_read(char *page, char **start, off_t offset,
					int count, int *eof, void *data);
/* Virtual file system write handler(/proc/kyocera_wifi/wifi_state) be add. */
static int kcj_wifi_state_write(struct file *file, const char *buffer,
					unsigned long count, void *data);

struct proc_dir_entry *kyocera_wifi_dir;

/* Wifi operation state ( WiFi_OFF , WiFi_Station_mode , WiFi_Hotspot) */
static int wifi_current_operation_state = 0;

struct pm8xxx_gpio_init_info {
	unsigned			gpio;
	struct pm_gpio			config;
};

static struct pm8xxx_gpio_init_info pmic_quickvx_clk_gpio = {
	PM8058_GPIO_PM_TO_SYS(PM8058_GPIO_SLEEP_CLK),
	{
		.direction      = PM_GPIO_DIR_OUT,
		.output_buffer  = PM_GPIO_OUT_BUF_CMOS,
		.output_value   = 1,
		.pull           = PM_GPIO_PULL_NO,
		.vin_sel        = PM8058_GPIO_VIN_S3,
		.out_strength   = PM_GPIO_STRENGTH_HIGH,
		.function       = PM_GPIO_FUNC_2,
	},
};

/* Wifi operation status. */
enum KCJ_WIFI_OPERATION_TYPE {
	KCJ_WIFI_OPERATION_WiFi_OFF = 0,     /* WiFi-OFF  */
	KCJ_WIFI_OPERATION_WiFi_STATION = 1, /* WiFi-Station mode */
	KCJ_WIFI_OPERATION_WiFi_HOTSPOT = 2, /* WiFi-Hotspot mode */
};

static int wifi_sleep_clock_initialize( void )
{
	int rc = 0;

	rc = pm8xxx_gpio_config(pmic_quickvx_clk_gpio.gpio,
				&pmic_quickvx_clk_gpio.config);
	if (rc) {
		pr_err("%s: pm8xxx_gpio_config(%#x)=%d\n",
			__func__, pmic_quickvx_clk_gpio.gpio,
			rc);
		return rc;
	}

	gpio_set_value_cansleep(PM8058_GPIO_PM_TO_SYS(
		PM8058_GPIO_SLEEP_CLK), 0);

	return rc;
}

int kyocera_wifi_init(void)
{
	int retval = 0;
	struct proc_dir_entry *ent;

	/* Creating directory "kyocera_wifi" entry */
	kyocera_wifi_dir = proc_mkdir("kyocera_wifi", NULL);
	if (kyocera_wifi_dir == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi directory");
		return -ENOMEM;
	}

	/* Creating read/write "power" entry */
	ent = create_proc_entry("power", 0666, kyocera_wifi_dir);
	if (ent == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi/power entry" );
		retval = -ENOMEM;
		goto fail;
	}
	ent->read_proc = kcj_wifi_power_initialize_read;
	ent->write_proc = kcj_wifi_power_initialize_write;

	/* Creating read/write "debug" entry */
	ent = create_proc_entry("debug", 0666, kyocera_wifi_dir);
	if (ent == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi/debug entry");
		retval = -ENOMEM;
		goto fail;
	}
	ent->read_proc = kcj_wifi_debug_read;
	ent->write_proc = kcj_wifi_debug_write;

	/* read only proc entries */
	if (create_proc_read_entry("bd_addr", 0444, kyocera_wifi_dir,
		bluepower_read_proc_bt_addr, NULL) == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi/bd_addr entry");
		retval = -ENOMEM;
		goto fail;
	}

	/* read only proc entries */
	if (create_proc_read_entry("wlan_mac_addr", 0444, kyocera_wifi_dir,
		bluepower_read_proc_wlan_mac_addr, NULL) == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi/wlan_mac_addr entry" );
		retval = -ENOMEM;
		goto fail;
	}

	/* read only proc entries */
	if (create_proc_read_entry("wlan_mac_addr_qcom", 0444, kyocera_wifi_dir,
		bluepower_read_proc_wlan_mac_addr_qcom, NULL) == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi/wlan_mac_addr_qcom entry" );
		retval = -ENOMEM;
		goto fail;
	}

	/* read only proc entries */
	if (create_proc_read_entry("product_line", 0444, kyocera_wifi_dir,
		read_proc_product_line, NULL) == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi/product_line entry" );
		retval = -ENOMEM;
		goto fail;
	}

	/* Creating read/write "wifi_state" entry */
	ent = create_proc_entry("wifi_state", 0666, kyocera_wifi_dir);
	if (ent == NULL) {
		pr_err("Unable to create /proc/kyocera_wifi/wifi_state entry" );
		retval = -ENOMEM;
		goto fail;
	}
	ent->read_proc = kcj_wifi_state_read;
	ent->write_proc = kcj_wifi_state_write;

	return 0;

fail:
	remove_proc_entry("wlan_mac_addr_qcom", kyocera_wifi_dir );
	remove_proc_entry("wifi_state", kyocera_wifi_dir );
	remove_proc_entry("product_line", kyocera_wifi_dir );
	remove_proc_entry("wlan_mac_addr", kyocera_wifi_dir );
	remove_proc_entry("bd_addr", kyocera_wifi_dir );
	remove_proc_entry("debug", kyocera_wifi_dir );
	remove_proc_entry("power", kyocera_wifi_dir );
	return retval;
}

static int kcj_wifi_power_initialize_read(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
	*eof = 1;
	return sprintf(page, "kcj_wifi_power_initialize_read\n");
}

static int kcj_wifi_power_initialize_write(struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	char *buf;

	if (count < 1)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		kfree(buf);
		return -EFAULT;
	}

	if (buf[0] == '0') {
/*		wifi_sleep_clock_initialize(); */
	} else if (buf[0] == '1') {
		wifi_sleep_clock_initialize();
		printk(KERN_WARNING "wifi_sleep_clock_initialize occured.\n");
	} else {
		kfree(buf);
		return -EINVAL;
	}

	kfree(buf);
	return count;
}

static int kcj_wifi_state_read(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
	/* Current WiFi status parameter would return. */
    return sprintf(page, "%d", wifi_current_operation_state );
}

static int kcj_wifi_state_write(struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	char *buf;

	if (count < 1)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		kfree(buf);
		return -EFAULT;
	}

	if (buf[0] == '0')
	{
		printk(KERN_WARNING "Wifi status is direct changed to 0.\n");
		kcj_wifi_set_current_operation_state(0);
	}
	else if (buf[0] == '1')
	{
		printk(KERN_WARNING "Wifi status is direct changed to 1.\n");
		kcj_wifi_set_current_operation_state(1);
	}
	else if (buf[0] == '2')
	{
		printk(KERN_WARNING "Wifi status is direct changed to 2.\n");
		kcj_wifi_set_current_operation_state(2);
	}
	else
	{
		kfree(buf);
		return -EINVAL;
	}

	kfree(buf);
	return count;
}


static int kcj_wifi_debug_read(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
	*eof = 1;
	return sprintf(page, "kcj_wifi_debug_read\n");
}

static int kcj_wifi_debug_write(struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	char *buf;

	if (count < 1)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		kfree(buf);
		return -EFAULT;
	}

	if (buf[0] == '0') {
	} else if (buf[0] == '1') {
		printk(KERN_WARNING "kcj_wifi_debug_write occured.\n");
	} else {
		kfree(buf);
		return -EINVAL;
	}

	kfree(buf);
	return count;
}

static int bluepower_read_proc_bt_addr(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
	unsigned int read_bd_addr[2];
	unsigned char write_bd_addr[6];
	int ret;
	unsigned int cmd;

	memset(read_bd_addr ,0 ,sizeof(read_bd_addr ));
	memset(write_bd_addr,0 ,sizeof(write_bd_addr));
	ret = -1;

	*eof = 1;

    cmd = 0;
	ret = msm_proc_comm(PCOM_CUSTOMER_CMD3  , &cmd
											,  &read_bd_addr[0]);

    if( ret != 0)
	{
		memset(read_bd_addr,0,sizeof(read_bd_addr));
	}
    cmd = 1;
	ret = msm_proc_comm(PCOM_CUSTOMER_CMD3  , &cmd
											, &read_bd_addr[1]);

	if( ret != 0)
	{
		memset(read_bd_addr,0,sizeof(read_bd_addr));
	}

	write_bd_addr[0] =  read_bd_addr[0] & 0x000000FF ;
	write_bd_addr[1] = (read_bd_addr[0] & 0x0000FF00 ) >> 8;
	write_bd_addr[2] = (read_bd_addr[0] & 0x00FF0000 ) >> 16;
	write_bd_addr[3] = (read_bd_addr[0] & 0xFF000000 ) >> 24;
	write_bd_addr[4] =  read_bd_addr[1] & 0x000000FF ;
	write_bd_addr[5] = (read_bd_addr[1] & 0x0000FF00 ) >> 8;

	return sprintf(page, "%02x:%02x:%02x:%02x:%02x:%02x", write_bd_addr[0], \
														  write_bd_addr[1], \
														  write_bd_addr[2], \
														  write_bd_addr[3], \
														  write_bd_addr[4], \
														  write_bd_addr[5]);
}

static int bluepower_read_proc_wlan_mac_addr_qcom(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
	return bluepower_read_proc_wlan_mac_addr_common(page, start, offset, count, eof, data , 0);
}

static int bluepower_read_proc_wlan_mac_addr(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
	return bluepower_read_proc_wlan_mac_addr_common(page, start, offset, count, eof, data , 1);
}

static int bluepower_read_proc_wlan_mac_addr_common(char *page, char **start, off_t offset,
					int count, int *eof, void *data , int format_mode)
{
	unsigned int read_wlan_mac_addr[2];
	unsigned char write_wlan_mac_addr[6];
	int ret;
	unsigned int cmd;

	memset(read_wlan_mac_addr ,0 ,sizeof(read_wlan_mac_addr ));
	memset(write_wlan_mac_addr,0 ,sizeof(write_wlan_mac_addr));
	ret = -1;
	*eof = 1;
    cmd = 2;
	ret = msm_proc_comm(PCOM_CUSTOMER_CMD3, &cmd, 
											&read_wlan_mac_addr[0]
			);
	if( ret != 0)
	{
		memset(read_wlan_mac_addr,0,sizeof(read_wlan_mac_addr));
	}
    cmd = 3;
	ret = msm_proc_comm(PCOM_CUSTOMER_CMD3, &cmd, 
											&read_wlan_mac_addr[1]
															);
    if(( ret != 0) ||
       ((0x00000000 == (read_wlan_mac_addr[0] & 0xFFFFFFFF)) &&
        (0x00000000 == (read_wlan_mac_addr[1] & 0x0000FFFF))
      ))
	{

		memset(read_wlan_mac_addr,0,sizeof(read_wlan_mac_addr));
		if(check_product_line() != 1)
		{
			return sprintf(page, "ZZZZZZZZ"); 
		}
		else{
			return sprintf(page, "100000000000"); 
		}
	}

	write_wlan_mac_addr[0] =  read_wlan_mac_addr[0] & 0x000000FF ;
	write_wlan_mac_addr[1] = (read_wlan_mac_addr[0] & 0x0000FF00 ) >> 8;
	write_wlan_mac_addr[2] = (read_wlan_mac_addr[0] & 0x00FF0000 ) >> 16;
	write_wlan_mac_addr[3] = (read_wlan_mac_addr[0] & 0xFF000000 ) >> 24;
	write_wlan_mac_addr[4] =  read_wlan_mac_addr[1] & 0x000000FF ;
	write_wlan_mac_addr[5] = (read_wlan_mac_addr[1] & 0x0000FF00 ) >> 8;

	/* If format_mode contains '1' , string will return for part of wifi station name. */
	if ( format_mode == 1 )
	{
		return sprintf(page, "macaddr=%02x:%02x:%02x:%02x:%02x:%02x", 
	                                                      write_wlan_mac_addr[0],
														  write_wlan_mac_addr[1],
														  write_wlan_mac_addr[2],
														  write_wlan_mac_addr[3],
														  write_wlan_mac_addr[4],
														  write_wlan_mac_addr[5]);
	}
	else 
	{
		return sprintf(page, "%02x%02x%02x%02x%02x%02x", 
	                                                      write_wlan_mac_addr[0],
														  write_wlan_mac_addr[1],
														  write_wlan_mac_addr[2],
														  write_wlan_mac_addr[3],
														  write_wlan_mac_addr[4],
														  write_wlan_mac_addr[5]);
	}
}

static int read_proc_product_line(char *page, char **start, off_t offset,
					int count, int *eof, void *data)
{
    int ret = 0;
    
    ret = check_product_line();
    
    return sprintf(page, "%d", ret);    
}

static int check_product_line(void)
{
    unsigned int read_product_line[2];
	unsigned char check_product[8];
	int ret;
	unsigned int cmd;

	memset(read_product_line ,0 ,sizeof(read_product_line ));
	memset(check_product,0 ,sizeof(check_product));
	ret = 0;

    cmd = 4;
	ret = msm_proc_comm(PCOM_CUSTOMER_CMD3, &cmd, 
											&read_product_line[0]
			);
	if( ret != 0)
	{
		memset(read_product_line,0,sizeof(read_product_line));
	}
    cmd = 5;
	ret = msm_proc_comm(PCOM_CUSTOMER_CMD3, &cmd, 
											&read_product_line[1]
			);

	if( ret != 0)
	{
		memset(read_product_line,0,sizeof(read_product_line));
	}

	check_product[0] =  read_product_line[0] & 0x000000FF ;
	check_product[1] = (read_product_line[0] & 0x0000FF00 ) >> 8;
	check_product[2] = (read_product_line[0] & 0x00FF0000 ) >> 16;
	check_product[3] = (read_product_line[0] & 0xFF000000 ) >> 24;
	check_product[4] =  read_product_line[1] & 0x000000FF ;
	check_product[5] = (read_product_line[1] & 0x0000FF00 ) >> 8;
	check_product[6] = (read_product_line[1] & 0x00FF0000 ) >> 16;
	check_product[7] = (read_product_line[1] & 0xFF000000 ) >> 24;
	ret = 1;

	return ret;
}

void kcj_wifi_set_current_operation_state(int state)
{
	switch (state)
	{
		case KCJ_WIFI_OPERATION_WiFi_OFF:
			// Wifi state changing would announce to mARM.
			kcj_dev_info_update_wlan ( DEV_INFO_WLAN_OFF );
			break;

		case KCJ_WIFI_OPERATION_WiFi_STATION:
			// Wifi state changing would announce to mARM.
			kcj_dev_info_update_wlan ( DEV_INFO_WLAN_ON );
			break;

		case KCJ_WIFI_OPERATION_WiFi_HOTSPOT:
			// Wifi state changing would announce to mARM.
			kcj_dev_info_update_wlan ( DEV_INFO_WLAN_HOTSPOT_ON );
			break;

		default:
			printk(KERN_WARNING "%s:not supprted state[%d].\n",__func__,state);
			break;
	}
	
	// Current state setting is stored for debugging.
	wifi_current_operation_state = state;

	printk(KERN_WARNING "--- Wifi state change occured.[%d]\n",state);

}
EXPORT_SYMBOL(kcj_wifi_set_current_operation_state);

