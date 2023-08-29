#include <linux/kernel.h> /* We're doing kernel work */ 

#include <linux/module.h> /* Specifically, a module */ 

#include <linux/proc_fs.h> /* Necessary because we use the proc fs */ 

#include <linux/uaccess.h> /* for copy_from_user */ 

#include <linux/version.h> 
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/signal.h>

#include <linux/kthread.h>
#include <linux/delay.h>
 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0) 

#define HAVE_PROC_OPS 

DEFINE_SPINLOCK(etx_spinlock);

#endif 

 

#define PROCFS_MAX_SIZE 1024 

#define PROCFS_NAME "sig_target" 

 

/* This structure hold information about the /proc file */ 

static struct proc_dir_entry *our_proc_file; 

 

/* The buffer used to store character for this module */ 

static char procfs_buffer[PROCFS_MAX_SIZE]; 

 

/* The size of the buffer */ 

static unsigned long procfs_buffer_size = 0; 

 

/* This function is called then the /proc file is read */ 

static ssize_t procfile_read(struct file *filePointer, char __user *buffer, 

                             size_t buffer_length, loff_t *offset) 

{ 

    char s[13] = "HelloWorld!\n"; 

    int len = sizeof(s); 

    ssize_t ret = len; 

 

    if (*offset >= len || copy_to_user(buffer, s, len)) { 

        pr_info("copy_to_user failed\n"); 

        ret = 0; 

    } else { 

        pr_info("procfile read %s\n", filePointer->f_path.dentry->d_name.name); 

        *offset += len; 

    } 

 

    return ret; 

} 

 
int fill = 0;

/* This function is called with the /proc file is written. */ 

static ssize_t procfile_write(struct file *file, const char __user *buff, 

                              size_t len, loff_t *off) 

{ 

    procfs_buffer_size = len; 

    if (procfs_buffer_size > PROCFS_MAX_SIZE) 

        procfs_buffer_size = PROCFS_MAX_SIZE; 

 

    if (copy_from_user(procfs_buffer, buff, procfs_buffer_size)) 

        return -EFAULT; 

 

    procfs_buffer[procfs_buffer_size & (PROCFS_MAX_SIZE - 1)] = '\0'; 

    *off += procfs_buffer_size; 

    pr_info("procfile write %s\n", procfs_buffer); 
    fill = 1;

 

    return procfs_buffer_size; 

} 

 

#ifdef HAVE_PROC_OPS 

static const struct proc_ops proc_file_fops = { 

    .proc_read = procfile_read, 

    .proc_write = procfile_write, 

}; 

#else 

static const struct file_operations proc_file_fops = { 

    .read = procfile_read, 

    .write = procfile_write, 

}; 

#endif 

int myAtoi(char* str)
{
    int res = 0;
    int sign = 1;
    int i = 0;

    if (str[0] == '-') {
        sign = -1;
        i++;
    }
  
    for (; str[i] != '\0'; i++)
        res = res * 10 + str[i] - '0';
  
    return sign * res;
}

void send(char *substring, char *substring2)
{
    struct task_struct *p2;
    struct pid *pid_s;
    pid_t pid;
    int sign;

    pid = myAtoi(substring);
    sign = myAtoi(substring2);
    pr_info("pid, signal is %d, %d\n", pid, sign);
    pid_s = find_vpid(pid);
    p2 = pid_task(pid_s, PIDTYPE_PID);
    send_sig(sign, p2, 0);
}


int checker(void* p)
{
    // time64_t t_var;
    int c, n, start, before;

    char substring[1024], substring2[1024], mystring[1025];

    while(!kthread_should_stop())
    {
        ssleep(1);
        // pr_info("buffer is %s\n", procfs_buffer);
        spin_lock(&etx_spinlock);
        c=0;
        start = 0;
        before = 0;
        pr_info("Checking buffer...");
        if(fill==1)
        {
            n = strlen(procfs_buffer);
            fill = 0;
            pr_info("change found\n");
        } 
        else
        {
            n = 0;
            pr_info("No Change\n");
        }
        for(int i = 0;i<n;i++)
            mystring[i] = procfs_buffer[i];
        mystring[n]= '\0';

        // if(spin_is_locked(&etx_spinlock))
        //     pr_info("YES\n");
        // else
        //     pr_info("NO\n");
 
        while (c<n) 
        {
            if(mystring[c]=='\n')
            {
                substring2[c-start] = '\0';
                start = c+1;
                before = 0;
                send(substring, substring2);
            }
            else
            {
                if(mystring[c]==',')
                {
                    substring[c-start] = '\0';
                    start = c+2;
                    c++;
                    before = 1;
                }
                else
                {
                    if(before==0)
                    {
                        substring[c-start] = mystring[c];
                    }
                    else
                    {
                        substring2[c-start] = mystring[c];
                    }
                }
            }
            c++;
        }
        spin_unlock(&etx_spinlock);
    }

    pr_info("The per-second checking thread has terminated\n");
    return 0;
}

 
static struct task_struct *kthread;

static int __init final_init(void) 

{ 

    our_proc_file = proc_create(PROCFS_NAME, 0666, NULL, &proc_file_fops); 

    if (NULL == our_proc_file) { 

        proc_remove(our_proc_file); 

        pr_alert("Error:Could not initialize /proc/%s\n", PROCFS_NAME); 

        return -ENOMEM; 

    } 

 
    kthread = kthread_run(checker, NULL, "counter");
    pr_info("/proc/%s created\n", PROCFS_NAME); 

    return 0; 

} 

 

static void __exit final_exit(void) 

{ 

    proc_remove(our_proc_file); 
    kthread_stop(kthread);
    pr_info("/proc/%s removed\n", PROCFS_NAME); 

} 

 

module_init(final_init); 

module_exit(final_exit); 

 

MODULE_LICENSE("GPL");