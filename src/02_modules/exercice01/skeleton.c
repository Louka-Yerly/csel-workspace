// skeleton.c
#include <linux/module.h>	// needed by all modules
#include <linux/init.h>		// needed for macros
#include <linux/kernel.h>	// needed for debugging

#include <linux/moduleparam.h>	// needed for module parameters

#include <linux/slab.h>
#include <linux/list.h>

static char* text = "dummy text";
module_param(text, charp, 0664);
static int  elements = 1;
module_param(elements, int, 0);

struct element {
    struct list_head list;
    int unique_id;
    char* text;
};

static LIST_HEAD (my_list);

static void print_elements(void) {
    struct element* ele;
    // iterate over the whole list
    list_for_each_entry(ele, &my_list, list) { 
        pr_info("  ->Element %i, text: %s\n", ele->unique_id, ele->text);
    }
}

void alloc_ele(int unique_id, char* text) {
    // create a new element
    struct element* ele = kzalloc(sizeof(*ele), GFP_KERNEL);
    ele->unique_id = unique_id;
    ele->text = text; 
    if (ele != NULL) {
        // add element at the end of the list 
        list_add_tail(&ele->list, &my_list);
    } else {
        pr_err("Memory cannot be allocated\n");
    }
}

static int __init skeleton_init(void)
{
    int i=0;

    pr_info ("Linux module 01 skeleton loaded\n");
    pr_debug ("  text: %s\n  elements: %d\n", text, elements);
    
    for(; i<elements; i++) {
        alloc_ele(i, text);
    }

    print_elements();

    return 0;
}

static void free_list(void) {
    struct element* ele;
	struct list_head *p, *n;
    
    // reference: https://stackoverflow.com/questions/63051548/linux-kernel-list-freeing-memory
	list_for_each_safe(p, n, &my_list) {
		ele = list_entry(p, struct element, list);
        pr_info("  ->Element %i freed", ele->unique_id);
        kfree(ele);
	}
    
}

static void __exit skeleton_exit(void)
{
    free_list();
    pr_info ("Linux module skeleton unloaded\n");
}

module_init (skeleton_init);
module_exit (skeleton_exit);

MODULE_AUTHOR ("Srdjenovic Luca & Yerly Louka");
MODULE_DESCRIPTION ("Module skeleton");
MODULE_LICENSE ("GPL");

