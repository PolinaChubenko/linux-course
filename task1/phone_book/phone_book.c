#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME "phone_book"
#define MAX_FIELD_SIZE 32
#define KBUF_SIZE 512

static dev_t first_dev;
static struct cdev *phone_book_cdev;
static unsigned int counter = 1;
static int major = 700;
static int minor = 0;
static char *kbuf;


//************ USER DATA ************//

struct user_data_node *head = NULL;

struct user_data {
    char name[MAX_FIELD_SIZE];
    char surname[MAX_FIELD_SIZE];
    size_t age;
    char number[MAX_FIELD_SIZE];
    char email[MAX_FIELD_SIZE];
};

struct user_data_node {
    struct user_data_node *next;
    struct user_data user;
};

//************ PHONE BOOK METHODS ************//

long _get_user(const char *surname, size_t len, struct user_data *output_data) {
    struct user_data_node *node = head;
    while (node != NULL) {
        if (strncmp(node->user.surname, surname, len) == 0) {
            memcpy(output_data, &node->user, sizeof(struct user_data));
            return 0;
        }
        node = node->next;
    }
    return -1;
}

long _add_user(struct user_data *input_data) {
    struct user_data_node *new_node = kmalloc(sizeof(struct user_data_node), GFP_KERNEL);
    memcpy(&new_node->user, input_data, sizeof(struct user_data));
    new_node->next = head;
    head = new_node;
    return 0;
}

long _del_user(const char *surname, size_t len) {
    struct user_data_node **next_of_prev_node = &head;
    struct user_data_node *node = head;
    while (node != NULL) {
        if (strncmp(node->user.surname, surname, len) == 0) {
            *next_of_prev_node = node->next;
            kfree(node);
            return 0;
        }
        next_of_prev_node = &node->next;
        node = node->next;
    }
    return -1;
}

//************ PARSER ************//
// template: '<operation> <params...>'

void parse_action(void) {
    struct user_data user;
    size_t len;
    size_t scan_margin = 2;
    char surname[MAX_FIELD_SIZE];
    if (kbuf[0] == '+') {
        sscanf(kbuf + scan_margin, "%s%s%zu%s%s",
               user.name, user.surname, &user.age, user.number, user.email);
        if (_add_user(&user) == 0) {
            snprintf(kbuf, KBUF_SIZE, "Added successfully\n");
        } else {
            snprintf(kbuf, KBUF_SIZE, "Failed to add\n");
        }
    } else if (kbuf[0] == '?') {
        sscanf(kbuf + scan_margin, "%s%ln", surname, &len);
        if (_get_user(surname, len, &user) == 0) {
            snprintf(kbuf, KBUF_SIZE, "Name: %s\nSurname: %s\nAge: %zu\nPhone number: %s\nEmail: %s\n",
                     user.name, user.surname, user.age, user.number, user.email);
        } else {
            snprintf(kbuf, KBUF_SIZE, "No such user in phone book\n");
        }
    } else if (kbuf[0] == '-') {
        sscanf(kbuf + scan_margin, "%s%ln", surname, &len);
        if (_del_user(surname, len) == 0) {
            snprintf(kbuf, KBUF_SIZE, "Removed successfully\n");
        } else {
            snprintf(kbuf, KBUF_SIZE, "Failed to remove\n");
        }
    } else {
        snprintf(kbuf, KBUF_SIZE, "Available actions:\n"
                                  "'+ <name> <surname> <age> <phone number> <email>' -- add user\n"
                                  "'- <surname>' -- delete user\n'? <surname>' -- find user\n");
    }
}

//************ CHARACTER DEVICE DRIVERS ************//

static int phone_book_open(struct inode *inode, struct file *file) {
    pr_info("Opening device %s\n\n", DEVICE_NAME);
    ++counter;
    return 0;
}

static int phone_book_close(struct inode *inode, struct file *file) {
    pr_info("Closing device %s\n\n", DEVICE_NAME);
    --counter;
    return 0;
}

static ssize_t phone_book_write(struct file *file, const char __user *buf, size_t lbuf, loff_t *ppos) {
    pr_info("Write device %s\n\n", DEVICE_NAME);
    if (KBUF_SIZE - *ppos < lbuf) {
        lbuf = KBUF_SIZE - *ppos;
    }
    copy_from_user(kbuf + *ppos, buf, lbuf);
    *ppos += lbuf;
    kbuf[*ppos] = '\0';
    parse_action();
    return lbuf;
}

static ssize_t phone_book_read(struct file *file, char __user *buf, size_t lbuf, loff_t *ppos) {
    size_t data_len = strlen(kbuf);
    pr_info("Read device %s\n\n", DEVICE_NAME);
    if (data_len - *ppos < lbuf) {
        lbuf = data_len - *ppos;
    }
    copy_to_user(buf, kbuf + *ppos, lbuf);
    *ppos += lbuf;
    return lbuf;
}

static const struct file_operations phone_book_fops = {
    .owner = THIS_MODULE,
    .read = phone_book_read,
    .write = phone_book_write,
    .open = phone_book_open,
    .release = phone_book_close
};

static int __init init_phone_book(void) {
    pr_info("Starting %s\n", DEVICE_NAME);
    kbuf = kmalloc(KBUF_SIZE, GFP_KERNEL);
    first_dev = MKDEV(major, minor);
    register_chrdev_region(first_dev, counter, DEVICE_NAME);
    phone_book_cdev = cdev_alloc();
    cdev_init(phone_book_cdev, &phone_book_fops);
    cdev_add(phone_book_cdev, first_dev, counter);
    return 0;
}

static void __exit cleanup_phone_book(void) {
    pr_info("Leaving %s\n", DEVICE_NAME);
    if (phone_book_cdev) {
        cdev_del(phone_book_cdev);
    }
    unregister_chrdev_region(first_dev, counter);
    kfree(kbuf);
}

//************ MODULE MARCOS ************//

module_init(init_phone_book);
module_exit(cleanup_phone_book);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chubenko Polina <chubenko.pn@phystech.edu>");

