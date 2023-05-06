#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>

#define DEVICE_NAME "keyboard_statistics"
#define KEYBOARD_IRQ 1
#define INTERVAL 60

static int irq = KEYBOARD_IRQ;
static unsigned int clicks_counter = 0;
static bool is_stopped = false;
static int dev_id;

struct timer_list print_result_timer;


static irqreturn_t handle_keyboard_click(int irq, void *dev) {
    ++clicks_counter;
    return IRQ_NONE;
}

void print_statistics(struct timer_list *timer) {
    pr_info("Number of clicks: %d\n", clicks_counter);
    if (!is_stopped) {
        clicks_counter = 0;
        mod_timer(&print_result_timer, jiffies + INTERVAL * HZ);
    }
}

//************ MODULE BASICS ************//

static int __init init_keyboard_statistics(void) {
    pr_info("Starting %s\n", DEVICE_NAME);
    if (request_irq(irq, handle_keyboard_click, IRQF_SHARED, "keyboard", &dev_id) == 0) {
        timer_setup(&print_result_timer, print_statistics, 0);
        mod_timer(&print_result_timer, jiffies + INTERVAL * HZ);
        return 0;
    }
    return -1;
}

static void __exit cleanup_keyboard_statistics(void) {
    pr_info("Leaving %s\n", DEVICE_NAME);
    synchronize_irq(irq);
    free_irq(irq, &dev_id);
    is_stopped = true;
    del_timer_sync(&print_result_timer);
}

//************ MODULE MARCOS ************//

module_init(init_keyboard_statistics);
module_exit(cleanup_keyboard_statistics);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chubenko Polina <chubenko.pn@phystech.edu>");

