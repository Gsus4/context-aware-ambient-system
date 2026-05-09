#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/delay.h>

/* ================= HW ================= */
#define PERI_BASE  0xFE000000
#define GPIO_BASE  (PERI_BASE + 0x200000)
#define PWM_BASE   (PERI_BASE + 0x20C000)
#define CLK_BASE   (PERI_BASE + 0x101000)

/* PWM regs */
#define PWM_CTL   0x00
#define PWM_RNG1  0x10
#define PWM_DAT1  0x14
#define CM_PWMCTL 0xA0
#define CM_PWMDIV 0xA4

/* ================= CONFIG ================= */
#define DEVICE_NAME "pwm_filter"
#define CLASS_NAME  "pwm_filter_class"

#define PWM_RANGE      200
#define PWM_DIV_VALUE  192

/* ================= GLOBAL ================= */
static int major;
static struct class *pwm_class;
static struct device *pwm_device;

static void __iomem *gpio_regs;
static void __iomem *pwm_regs;
static void __iomem *clk_regs;

/* ================= MINIMAL STATE ================= */
/* 只保留必要 runtime value（不是 control state） */
static int current_output = 0;

/* ================= PWM OUTPUT ================= */
static void set_pwm_duty(int percent)
{
    u32 duty;

    if (!pwm_regs)
        return;

    /* clamp */
    if (percent > 100) percent = 100;
    if (percent < 0)   percent = 0;

    /* linear mapping */
    duty = (percent * PWM_RANGE) / 100;

    iowrite32(duty, pwm_regs + PWM_DAT1);
}

/* ================= WRITE INTERFACE ================= */
static ssize_t dev_write(struct file *file,
                         const char __user *buf,
                         size_t len,
                         loff_t *off)
{
    char kbuf[16];
    int v;

    if (len > 15)
        len = 15;

    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;

    kbuf[len] = '\0';

    if (kstrtoint(kbuf, 10, &v) < 0)
        return -EINVAL;

    /* ===== only accept 0~100 ===== */
    if (v < 0) v = 0;
    if (v > 100) v = 100;

    current_output = v;
    set_pwm_duty(current_output);

    return len;
}

/* ================= FILE OPS ================= */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = dev_write,
};

/* ================= INIT ================= */
static int __init pwm_init(void)
{
    u32 fsel;

    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0)
        return major;

    pwm_class = class_create(CLASS_NAME);
    pwm_device = device_create(pwm_class, NULL,
                               MKDEV(major, 0),
                               NULL, DEVICE_NAME);

    gpio_regs = ioremap(GPIO_BASE, 0x100);
    pwm_regs  = ioremap(PWM_BASE, 0x100);
    clk_regs  = ioremap(CLK_BASE, 0x100);

    /* GPIO18 ALT5 */
    fsel = ioread32(gpio_regs + 4);
    fsel &= ~(7 << 24);
    fsel |= (2 << 24);
    iowrite32(fsel, gpio_regs + 4);

    /* PWM clock setup */
    iowrite32(0x5A000001, clk_regs + CM_PWMCTL);
    mdelay(10);

    iowrite32(0x5A000000 | (PWM_DIV_VALUE << 12),
              clk_regs + CM_PWMDIV);

    mdelay(10);
    iowrite32(0x5A000011, clk_regs + CM_PWMCTL);

    /* PWM init */
    iowrite32(PWM_RANGE, pwm_regs + PWM_RNG1);
    iowrite32(0, pwm_regs + PWM_DAT1);
    iowrite32(0x81, pwm_regs + PWM_CTL);

    printk(KERN_INFO "PWM_FILTER V3: clean actuator driver loaded\n");
    return 0;
}

/* ================= EXIT ================= */
static void __exit pwm_exit(void)
{
    set_pwm_duty(0);

    iounmap(gpio_regs);
    iounmap(pwm_regs);
    iounmap(clk_regs);

    device_destroy(pwm_class, MKDEV(major, 0));
    class_destroy(pwm_class);
    unregister_chrdev(major, DEVICE_NAME);

    printk(KERN_INFO "PWM_FILTER V3: unloaded\n");
}

module_init(pwm_init);
module_exit(pwm_exit);
MODULE_LICENSE("GPL");
