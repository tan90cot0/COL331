#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include "linux/spinlock.h"
#include <linux/fs.h>

#define MINORS 2

static int mychardev_open(struct inode *inode, struct file *file);
static int mychardev_release(struct inode *inode, struct file *file);
static ssize_t mychardev_read(struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t mychardev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);

DEFINE_SPINLOCK(etx_spinlock);
spinlock_t etx_spinlock;
//spin_lock_init(&etx_spinlock);

static const struct file_operations mychardev_fops = {
    .owner      = THIS_MODULE,
    .open       = mychardev_open,
    .release    = mychardev_release,
    .read       = mychardev_read,
    .write       = mychardev_write
};

struct mychar_device_data {
    struct cdev cdev;
};

static int dev_major = 0;
static struct class *mychardev_class = NULL;
static struct mychar_device_data mychardev_data[MINORS];
char device_data[1048576];
int current_index = 0;
int read_index = 0;

static int mychardev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

static int __init mychardev_init(void)
{
    dev_t dev;

    alloc_chrdev_region(&dev, 0, MINORS, "mychardev");

    dev_major = MAJOR(dev);

    mychardev_class = class_create(THIS_MODULE, "mychardev");
    mychardev_class->dev_uevent = mychardev_uevent;

    for (int i = 0; i < MINORS; i++) {
        cdev_init(&mychardev_data[i].cdev, &mychardev_fops);
        mychardev_data[i].cdev.owner = THIS_MODULE;

        cdev_add(&mychardev_data[i].cdev, MKDEV(dev_major, i), 1);

        device_create(mychardev_class, NULL, MKDEV(dev_major, i), NULL, "mychardev-%d", i);
    }

    return 0;
}

static void __exit mychardev_exit(void)
{
    int i;

    for (i = 0; i < MINORS; i++) {
        device_destroy(mychardev_class, MKDEV(dev_major, i));
    }

    class_unregister(mychardev_class);
    class_destroy(mychardev_class);

    unregister_chrdev_region(MKDEV(dev_major, 0), MINORMASK);
}

static int mychardev_open(struct inode *inode, struct file *file)
{
    printk("MYCHARDEV: Device open\n");
    return 0;
}

static int mychardev_release(struct inode *inode, struct file *file)
{
    printk("MYCHARDEV: Device close\n");
    return 0;
}

char *strrev(char *str)
{
      char *p1, *p2;

      if (! str || ! *str)
            return str;
      for (p1 = str, p2 = str + strlen(str) - 1; p2 > p1; ++p1, --p2)
      {
            *p1 ^= *p2;
            *p2 ^= *p1;
            *p1 ^= *p2;
      }
      return str;
}

char * strcat(char *dest, const char *src)
{
    int i,j;
    for (i = 0; dest[i] != '\0'; i++)
        ;
    for (j = 0; src[j] != '\0'; j++)
        dest[i+j] = src[j];
    dest[i+j] = '\0';
    return dest;
}

static ssize_t mychardev_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    char* temp2;
    char* temp;
    int dev_minor;
    size_t datalen;
    temp2 = kstrdup(device_data, GFP_KERNEL);
    temp = strrev(temp2);
    temp = temp + read_index;
    datalen = strlen(temp);
    printk("%s", temp);

    dev_minor = MINOR(file->f_path.dentry->d_inode->i_rdev);
    //check if the device is the reading device

    if(datalen==0)
    {
        temp = "$";
        datalen = 1;
    }

    if(dev_minor==0)
    {
        if (count > datalen)
            count = datalen;

        if (copy_to_user(buf, temp, count))
            return -EFAULT;

        read_index+=count;
    }

    return count;
}

static ssize_t mychardev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
    size_t ncopied;
    size_t maxdatalen = 1048576;

    int dev_minor = MINOR(file->f_path.dentry->d_inode->i_rdev);
    //check if the device is the writing device

    if(dev_minor==1)
    {
        printk("count = %ld, maxdatalen = %ld\n", count, maxdatalen);
        if (count < maxdatalen) {
            maxdatalen = count;
        }

        spin_lock(&etx_spinlock);
        ncopied = copy_from_user(&device_data[current_index], buf, maxdatalen);
        spin_unlock(&etx_spinlock);

        if (ncopied == 0) {
            printk("Copied %zd bytes from the user\n", maxdatalen);
        } else {
            printk("Could't copy %zd bytes from the user\n", ncopied);
        }

        current_index+=maxdatalen;
        device_data[current_index] = 0;
        printk("Total Data: %s\n", device_data);
    }
    else
    {
        printk("Devices other than 1 cannot write\n");
    }
    

    return count;
}

MODULE_LICENSE("GPL");

module_init(mychardev_init);
module_exit(mychardev_exit);