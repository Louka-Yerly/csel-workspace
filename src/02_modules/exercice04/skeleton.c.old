/* skeleton.c */
#include <linux/module.h>	/* needed by all modules */
#include <linux/init.h>		/* needed for macros */
#include <linux/kernel.h>	/* needed for debugging */

#include <linux/moduleparam.h>	/* needed for module parameters */

#include <linux/slab.h>		/* needed for dynamic memory allocation */
#include <linux/list.h>		/* needed for linked list processing */
#include <linux/string.h>	/* needed for string handling */

static char* text = "dummy text";
module_param(text, charp, 0);
static int  elements = 0;
module_param(elements, int, 0);

struct element {
	char text[100];
	int ele_nr;
	struct list_head node;
};

static LIST_HEAD (my_list);

static int __init skeleton_init(void)
{
	int i;
	pr_info ("Linux module 04 skeleton loaded\n");
	pr_info ("  text: %s\n  elements: %d\n", text, elements);
	for (i = 0; i < elements; i++) {
		struct element* ele = kzalloc (sizeof(*ele), GFP_KERNEL);
		if (ele != 0) {
			strncpy (ele->text, text, 99);
			ele->ele_nr = i;
			list_add_tail (&ele->node, &my_list);
		}
	}
	return 0;
}

static void __exit skeleton_exit(void)
{
	struct element* ele;
	int nb_eles = 0;
	list_for_each_entry (ele, &my_list, node) {
		pr_info ("ele [%d/%d/%d] = %s\n", nb_eles, ele->ele_nr, elements, ele->text); 
		nb_eles++;
	}
	while (!list_empty (&my_list)) {
		ele = list_entry (my_list.next, struct element, node);
		list_del (&ele->node);
		kfree (ele);
	}
	pr_info ("All elements (%d/%d) of the list have been removed and deleted!\n", nb_eles, elements);
	pr_info ("Linux module skeleton unloaded\n");
}

module_init (skeleton_init);
module_exit (skeleton_exit);

MODULE_AUTHOR ("Daniel Gachet <daniel.gachet@hefr.ch>");
MODULE_DESCRIPTION ("Module skeleton");
MODULE_LICENSE ("GPL");

