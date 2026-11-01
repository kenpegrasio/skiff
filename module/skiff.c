// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>

static int __init skiff_init(void);
static int skiff_init(void) 
{
    pr_info("Hello, world!\n");
    return 0;
}

static void __exit skiff_exit(void);
static void skiff_exit(void) 
{
    pr_info("Exit from Skiff\n");
}

module_init(skiff_init);
module_exit(skiff_exit);

MODULE_LICENSE("GPL");
