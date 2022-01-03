// SPDX-License-Identifier: GPL-2.0

#include <sound/opl3.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/parport.h>
#include <linux/uaccess.h>

#define OPL3LPT_NAME "opl3lpt"

static unsigned int ioport[SNDRV_CARDS];
static unsigned int parportnum[SNDRV_CARDS];
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static struct parport *opl3lpt_parport[SNDRV_CARDS];
static struct pardevice *opl3lpt_pardevice[SNDRV_CARDS];
struct platform_device *opl3lpt_platform_device[SNDRV_CARDS];
struct snd_card *opl3lpt_card[SNDRV_CARDS];
struct snd_opl3 *opl3lpt_opl3[SNDRV_CARDS];
struct snd_hwdep *opl3lpt_hwdep[SNDRV_CARDS];

static int device_number = 0;

module_param_array(ioport, uint, NULL, 0444);
MODULE_PARM_DESC(ioport, "I/O address of parallel port where the OPL3LPT is connected (overrides parportnum if nonzero)");

module_param_array(parportnum, uint, NULL, 0444);
MODULE_PARM_DESC(parportnum, "Parallel port the OPL3LPT is connected to, e.g. 0 for parport0");

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for OPL3LPT parallel port soundcard");

module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for OPL3LPT parallel port soundcard");


struct opl3lpt {
	struct snd_card *card;
	struct snd_opl3 *opl3;
	struct snd_hwdep *hwdep;
	struct parport *parport;
	struct pardevice *pardevice;
};

static void opl3lpt_command(struct snd_opl3 * opl3, unsigned short cmd, unsigned char val)
{
	unsigned long flags;
	unsigned long port;
	struct opl3lpt *opl3lpt = (struct opl3lpt*)(opl3->private_data);
	struct parport *opl3_parport = opl3lpt->parport;

	port = (cmd & OPL3_RIGHT) ? opl3->r_port : opl3->l_port;

	spin_lock_irqsave(&opl3->reg_lock, flags);

	parport_write_data(opl3_parport, cmd);
	parport_write_control(opl3_parport, (PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT | PARPORT_CONTROL_STROBE));
	parport_write_control(opl3_parport, (PARPORT_CONTROL_SELECT | PARPORT_CONTROL_STROBE));
	parport_write_control(opl3_parport, (PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT | PARPORT_CONTROL_STROBE));

	udelay(10);

	parport_write_data(opl3_parport, val);
	parport_write_control(opl3_parport, (PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT));
	parport_write_control(opl3_parport, (PARPORT_CONTROL_SELECT));
	parport_write_control(opl3_parport, (PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT));

	udelay(33);

	spin_unlock_irqrestore(&opl3->reg_lock, flags);
}

static unsigned char opl3lpt_status(struct snd_opl3 * opl3)
{
	/* The OPL3LPT is write only, so just return 0. */
	return 0;
}

static unsigned char opl2lpt_status(struct snd_opl3 *opl3)
{
	/* The OPL2LPT is write only, so return the magic value 6. */
	return 0x06;
}

static int opl3lpt_probe(struct platform_device *device)
{
	struct pardev_cb opl3lpt_cb;
	struct opl3lpt *opl3lpt;
	struct parport *port;
	int error;

	port = (struct parport*)platform_get_drvdata(device);
	platform_set_drvdata(device, NULL);

	pr_err("%s: Initialising opl3lpt %d at port %i base %lu\n", OPL3LPT_NAME, device_number, port->number, port->base);


	opl3lpt = kzalloc(sizeof(*opl3lpt), GFP_KERNEL);
	if (opl3lpt == NULL) {
		pr_err("ERROR: Out of memory creating opl3lpt!\n");
		return -ENOMEM;
	}

	opl3lpt->parport = port;

	/* Register and claim the parallel port. */
	memset(&opl3lpt_cb, 0, sizeof(opl3lpt_cb));
	opl3lpt_cb.flags = PARPORT_DEV_EXCL;
	opl3lpt->pardevice = parport_register_dev_model(port, OPL3LPT_NAME,
						       &opl3lpt_cb, 0);
	if (!opl3lpt->pardevice) {
		pr_err("ERROR: parport didn't register new device\n");
		goto err_unreg_device;
	}
	if (parport_claim(opl3lpt->pardevice)) {
		pr_err("could not claim access to parport %i. Aborting.\n",
		      port->number);
		goto err_unreg_device;
	}

	if (snd_card_new(&device->dev, index[device->id], id[device->id], THIS_MODULE, 0, &opl3lpt->card)) {
		pr_err("ERROR: Couldn't register new sound card\n");
		goto err_unreg_device;
	}

	strcpy(opl3lpt->card->driver, OPL3LPT_NAME);
	strcpy(opl3lpt->card->shortname, "opl3lpt");
	sprintf(opl3lpt->card->longname, "opl3lpt on parport%d", opl3lpt->parport->number);

	/* Create a new OPL3 device */
	/* For now, this is always actually an OPL2, as that's all I have */
	if (snd_opl3_new(opl3lpt->card, OPL3_HW_OPL2, &opl3lpt->opl3)) {
		pr_err("ERROR: Couldn't create new OPL3 object\n");
		goto err_unreg_device;
	}

	/* Set up callbacks and private data for the OPL3. */
	opl3lpt->opl3->command = opl3lpt_command;
	opl3lpt->opl3->status = opl3lpt_status;
	opl3lpt->opl3->private_data = opl3lpt;

	/* Initialise the OPL3 chip. */
	snd_opl3_init(opl3lpt->opl3);

	/* err check */
	pr_err("%s: snd_opl3_hwdep_new\n", OPL3LPT_NAME);
	snd_opl3_hwdep_new(opl3lpt->opl3, 0, 0, &opl3lpt->hwdep);

	snd_opl3_reset(opl3lpt->opl3);
	pr_err("%s: Finished initialising opl3lpt.\n", OPL3LPT_NAME);


	error = snd_card_register(opl3lpt->card);
	if (error < 0) {
		pr_err("%s: Failed to register card: %d\n", OPL3LPT_NAME, error);
		goto err_close_opl3;
	}

	platform_set_drvdata(device, opl3lpt);
	return 0;
	
err_close_opl3:

	snd_opl3_reset(opl3lpt->opl3);
err_free_card:
	snd_card_disconnect(opl3lpt->card);
	snd_card_free_when_closed(opl3lpt->card);
err_unreg_device:
	parport_unregister_device(opl3lpt->pardevice);
	kfree(opl3lpt);
	opl3lpt = NULL;
	pr_err("%s: Failed to init.\n", OPL3LPT_NAME);
	return -ENODEV;
}

static int opl3lpt_pdev_remove(struct platform_device *dev)
{
	struct opl3lpt *opl3lpt = platform_get_drvdata(dev);

	snd_card_free_when_closed(opl3lpt->card);
	parport_release(opl3lpt->pardevice);

	kfree(opl3lpt);
	platform_set_drvdata(dev, NULL);
	return 0;
}

static struct platform_driver opl3lpt_platform = {
	.probe  = opl3lpt_probe,
	.remove = opl3lpt_pdev_remove,
	.driver = {
		.name = "opl3lpt"
	},
};

static void opl3lpt_match_port(struct parport *port)
{
	struct platform_device *device;

	/* Is this the port the user requested? */
	if (ioport[device_number] && ioport[device_number] != port->base)
		return;
	else if (parportnum[device_number] != port->number)
		return;

	/* Do we have room for another card? */
	if (device_number >= SNDRV_CARDS) {
		pr_err("%s: No free card devices\n", OPL3LPT_NAME);
		return;
	}

	/* We have one platform device per card. */
	device = platform_device_alloc(OPL3LPT_NAME, device_number);
	if (!device) {
		pr_err("%s: Couldn't create platform device.\n", OPL3LPT_NAME);
		return;
	}

	/* Temporary assignment to forward the parport */
	platform_set_drvdata(device, port);

	/* Add the platform device, and trigger _probe. */
	if (platform_device_add(device) < 0) {
		pr_err("%s: Couldn't add platform device.\n", OPL3LPT_NAME);
		platform_device_put(device);
		return;
	}

	/* If there's something in the drvdata, we've succeeded. */
	if (!platform_get_drvdata(device)) {
		/* Don't print an error here, as we did so in _probe */
		platform_device_unregister(device);
		return;
	}

	/* register device in global table */
	opl3lpt_platform_device[device_number] = device;
	device_number++;
}


static void opl3lpt_detach(struct parport *port)
{
}


static struct parport_driver opl3lpt_parport_driver = {
	.name = "opl3lpt",
	.match_port = opl3lpt_match_port,
	.detach = opl3lpt_detach,
	.devmodel = true,
};

/*********************************************************************
 * module init stuff
 *********************************************************************/
static void opl3lpt_shutdown(void)
{
	int i;

	for (i = 0; i < SNDRV_CARDS; ++i) {
		if (opl3lpt_platform_device[i]) {
			platform_device_unregister(opl3lpt_platform_device[i]);
			opl3lpt_platform_device[i] = NULL;
		}
	}		
	platform_driver_unregister(&opl3lpt_platform);
	parport_unregister_driver(&opl3lpt_parport_driver);
}

static int opl3lpt_module_init(void)
{
	int err;

	err = platform_driver_register(&opl3lpt_platform);
	if (err < 0)
		return err;

	if (parport_register_driver(&opl3lpt_parport_driver) != 0) {
		platform_driver_unregister(&opl3lpt_platform);
		return -EIO;
	}

	if (device_number == 0) {
		opl3lpt_shutdown();
		return -ENODEV;
	}

	return 0;
}

static void opl3lpt_module_exit(void)
{
	opl3lpt_shutdown();
}

module_init(opl3lpt_module_init);
module_exit(opl3lpt_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Gow <david@ingeniumdigital.com>");
MODULE_DESCRIPTION("OPL3LPT Parallel Port Synth Driver");

