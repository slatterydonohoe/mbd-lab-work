/* LAB4 AUDIO WITH FIFO */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <asm/spinlock.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/irqflags.h>
#include <asm/io.h>
#include <linux/types.h>

#define DRIVER_NAME "esl-audio"
#define AUDIO_WRITE_BUF_SIZE 64

#define FIFO_INT_ENABLE   0x4
#define FIFO_TX_RESET     0x8
#define FIFO_TX_VACANCY   0xC
#define FIFO_TX_DATA      0x10
#define FIFO_RX_RESET     0x18
#define FIFO_STREAM_RESET 0x28

#define FIFO_RESET_VAL 		0xA5
#define FIFO_TXOVERRUN_VAL 	(1 << 28)
#define FIFO_TXEMPTY_VAL 	(1 << 21)

// our driver
struct esl_audio_instance
{
  void* __iomem regs; //fifo registers
  struct cdev chr_dev; //character device
  dev_t devno;

  // list head
  struct list_head inst_list;

  // interrupt number
  unsigned int irqnum;

  // fifo depth
  unsigned int tx_fifo_depth;

  // wait queue
  wait_queue_head_t waitq;
};

// out global data
struct esl_audio_driver
{
  dev_t first_devno;
  struct class* class;
  unsigned int instance_count;
  struct list_head instance_list;
};

// Initialize global data
static struct esl_audio_driver driver_data = {
  .instance_count = 0,
  .instance_list = LIST_HEAD_INIT(driver_data.instance_list),
};

/* Utility Functions */
// find instance from inode using minor number and linked list
static struct esl_audio_instance* inode_to_instance(struct inode* i)
{
  struct esl_audio_instance *inst_iter;
  unsigned int minor = iminor(i);

  list_for_each_entry(inst_iter, &driver_data.instance_list, inst_list)
    {
      if (MINOR(inst_iter->devno) == minor)
        {
          // found our corresponding instance
          return inst_iter;
        }
    }

  // not found
  return NULL;
}

static struct esl_audio_instance* file_to_instance(struct file* f)
{
  return inode_to_instance(f->f_path.dentry->d_inode);
}

/* @brief Check if FIFO is full
   @return 0 if not full, "true" otherwise */
static unsigned int fifo_full(struct esl_audio_instance* inst)
{
  // Wait for room for our buffer size, we're going to fill up that many sample at once.
  return !(ioread32(inst->regs + FIFO_TX_VACANCY) > AUDIO_WRITE_BUF_SIZE);
}

/* Character device File Ops */
static ssize_t esl_audio_write(struct file* f,
                               const char __user *buf, size_t len,
                               loff_t* offset)
{
  struct esl_audio_instance *inst = file_to_instance(f);
  unsigned int written = 0;
  unsigned int temp_buf[AUDIO_WRITE_BUF_SIZE]; //32?
  int i;
  int bytes_to_copy;

  //printk(KERN_INFO "Wrote %d bytes to character device\n", len);

  if (!inst)
    {
      // instance not found
      return -ENOENT;
    }

  // Implement write to AXI FIFO
  while(written < len)
  {
	  wait_event_interruptible(inst->waitq, !(fifo_full(inst)));

	  if(len < AUDIO_WRITE_BUF_SIZE)
	  {
		  bytes_to_copy = len - written;
	  }
	  else
	  {
		  bytes_to_copy = AUDIO_WRITE_BUF_SIZE * sizeof(unsigned int);
	  }

	  copy_from_user(temp_buf, buf + written, bytes_to_copy);

  	  for(i = 0; i < bytes_to_copy/(sizeof(unsigned int)); i++)
  	  {
  		// Lab 4.4.2) polling in kernel has worse impact than in user space.
  		// Kernel has a higher priority and will waste system time that could be doing other things.
  		// removing the sleep grinds the system to a halt until the audio is played. Top shows the process
  		// using 100% cpu with no sleep and ~16-30% of the cpu with the sleep in place
  		// Lab 4.4.4) Stress has no impact on low rate stuff like hal audio, but makes higher sample rate
  		// things skip slightly

  	  	//while(fifo_full(inst))
  	  	//{
  		//  usleep_range(19, 21);
  	  	//}

  		iowrite32(temp_buf[i], inst->regs + FIFO_TX_DATA);
  	  }

  	  written += bytes_to_copy;
  }

  return written;
}

static int device_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "Hello from open\n");

	return 0;
}

struct file_operations esl_audio_fops = {
  .write = esl_audio_write,
  .open = device_open,
};

/* interrupt handler */
static irqreturn_t esl_audio_irq_handler(int irq, void* dev_id)
{
  struct esl_audio_instance* inst = dev_id;
  int intval;

  // read interrupt status regsiter
  intval = ioread32(inst->regs);

  // handle tx overrun
  if(intval & FIFO_TXOVERRUN_VAL)
  {
	  // reset tx fifo
	  iowrite32(FIFO_RESET_VAL, inst->regs + FIFO_TX_RESET);

	  // clear tx overrun interrupt in interrupt status register
	  iowrite32(FIFO_TXOVERRUN_VAL, inst->regs);

	  //printk(KERN_INFO "OVERRUN! \n");

	  intval &= ~FIFO_TXOVERRUN_VAL;
  }

  if(intval & FIFO_TXEMPTY_VAL)
  {
	  // clear tx empty interrput in interrput status register
	  iowrite32(FIFO_TXEMPTY_VAL, inst->regs);
	  // wake up module
	  wake_up(&(inst->waitq));

	 intval &= ~FIFO_TXEMPTY_VAL;
  }

  //printk("Hello from IRQ %08x\n", intval);

  iowrite32(0xFFFFFFFF, inst->regs);

  return IRQ_HANDLED;
}

static int esl_audio_probe(struct platform_device* pdev)
{
  struct esl_audio_instance* inst = NULL;
  int err;
  struct resource* res;
  struct device *dev;

  printk(KERN_INFO "Hello from probe\n");

  // allocate instance
  inst = devm_kzalloc(&pdev->dev, sizeof(struct esl_audio_instance),
                      GFP_KERNEL);

  // set platform driver data
  platform_set_drvdata(pdev, inst);

  // get registers (AXI FIFO)
  res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (IS_ERR(res))
    {
      return PTR_ERR(res);
    }

  inst->regs = devm_ioremap_resource(&pdev->dev, res);
  if (IS_ERR(inst->regs))
    {
      return PTR_ERR(inst->regs);
    }

  // get TX fifo depth
  err = of_property_read_u32(pdev->dev.of_node, "xlnx,tx-fifo-depth",
                             &inst->tx_fifo_depth);
  if (err)
    {
      printk(KERN_ERR "%s: failed to retrieve TX fifo depth\n",
             DRIVER_NAME);
      return err;
    }

  // get interrupt
  res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
  if (IS_ERR(res))
    {
      return PTR_ERR(res);
    }

  err = devm_request_irq(&pdev->dev, res->start,
                         esl_audio_irq_handler,
                         IRQF_TRIGGER_HIGH,
                         "zedaudio", inst);
  if (err < 0)
    {
      return err;
    }

  // save irq number
  inst->irqnum = res->start;

  // create character device
  // get device number
  inst->devno = MKDEV(MAJOR(driver_data.first_devno),
                      driver_data.instance_count);

  // TODO initialize and create character device
  // Initalize our character device echo_dev with default parameters and
  // the file operations in the struct file_operations named echo_fops
  // This sets the read and write functions to echo_read and echo_write
  cdev_init(&(inst->chr_dev), &esl_audio_fops);

  // Add our character device echo_dev to the system given the device number
  // we allocated above with alloc_chrdev_region. Ensure that there is only 1
  // echo device, as we only allocated enough space for one above
  err = cdev_add(&(inst->chr_dev), inst->devno, 1);
  if (err)
  {
	  return err;
  }

  // Create our device in the sysfs with no parent as a member of class "zedaudio"
  // The device number is given by echo_devno. We pass no private data and name it zedaudioN
  dev = device_create(driver_data.class,
				  	  NULL,
					  inst->devno,
					  NULL,
					  "zedaudio%d", driver_data.instance_count);
  if(IS_ERR(dev))
  {
	  device_destroy(driver_data.class, inst->devno);
	  cdev_del(&(inst->chr_dev));
	  return PTR_ERR(dev);
  }

  printk(KERN_INFO "zedaudio device created: Maj %d, Min %d\n", MAJOR(inst->devno), MINOR(inst->devno));

  // increment instance count
  driver_data.instance_count++;

  // put into list
  INIT_LIST_HEAD(&inst->inst_list);
  list_add(&inst->inst_list, &driver_data.instance_list);

  // init wait queue
  init_waitqueue_head(&inst->waitq);

  // reset AXI FIFO
  // reset value for these registers is 0xA5 from datasheet
  iowrite32(FIFO_RESET_VAL, inst->regs + FIFO_STREAM_RESET);
  iowrite32(FIFO_RESET_VAL, inst->regs + FIFO_TX_RESET);
  iowrite32(FIFO_RESET_VAL, inst->regs + FIFO_RX_RESET);

  // enable interrupts
  iowrite32(FIFO_TXOVERRUN_VAL | FIFO_TXEMPTY_VAL, inst->regs + FIFO_INT_ENABLE);

  return 0;
}

static int esl_audio_remove(struct platform_device* pdev)
{
  struct esl_audio_instance* inst = platform_get_drvdata(pdev);

  // TODO remove all traces of character device
  device_destroy(driver_data.class, inst->devno);
  cdev_del(&(inst->chr_dev));

  // remove from list
  list_del(&inst->inst_list);

  return 0;
}

// matching table
static struct of_device_id esl_audio_of_ids[] = {
  { .compatible = "esl,audio-fifo" },
  { }
};

// platform driver definition
static struct platform_driver esl_audio_driver = {
  .probe = esl_audio_probe,
  .remove = esl_audio_remove,
  .driver = {
    .name = DRIVER_NAME,
    .of_match_table = of_match_ptr(esl_audio_of_ids),
  },
};

static int __init esl_audio_init(void)
{
  int err;

  // alocate character device region
  err = alloc_chrdev_region(&driver_data.first_devno, 0, 16, "zedaudio");
  if (err < 0)
    {
      return err;
    }

  // create class
  // although not using sysfs, still necessary in order to automatically
  // get device node in /dev
  driver_data.class = class_create(THIS_MODULE, "zedaudio");
  if (IS_ERR(driver_data.class))
    {
      return -ENOENT;
    }

  platform_driver_register(&esl_audio_driver);

  return 0;
}

static void __exit esl_audio_exit(void)
{
  platform_driver_unregister(&esl_audio_driver);

  // free character device region
  unregister_chrdev_region(driver_data.first_devno, 16);

  // remove class
  class_destroy(driver_data.class);
}

module_init(esl_audio_init);
module_exit(esl_audio_exit);

MODULE_DESCRIPTION("ZedBoard Simple Audio driver");
MODULE_LICENSE("GPL");
