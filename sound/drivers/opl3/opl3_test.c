// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit test for OPL3-based synth drivers
 *
 * Copyright (C) 2021, David Gow <david@ingeniumdigital.com>
 */

#include <sound/opl3.h>
#include <sound/initval.h>
#include <kunit/test.h>

#include <sound/core.h>
#include <linux/platform_device.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;

static int fake_platform_probe(struct platform_device *dev)
{
	return 0;
}

static int fake_platform_remove(struct platform_device *dev)
{
	return 0;
}

static int fake_platform_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int fake_platform_resume(struct platform_device *dev)
{
	return 0;
}

static struct platform_driver fake_platform = {
	.probe = fake_platform_probe,
	.remove = fake_platform_remove,
	.suspend = fake_platform_suspend,
	.resume = fake_platform_resume,
	.driver = {
		.name = "opl3_test"
	},
};

/* An array of "expected writes" can be set, which will be EXPECTED */
struct expected_write
{
	unsigned short cmd;
	unsigned short val;
};

static struct expected_write *current_write_ptr = NULL;

/* Complete state of both left and right synths. */
static unsigned short fake_opl3_state[512] = {0};

static void fake_opl3_command(struct snd_opl3 * opl3, unsigned short cmd, unsigned char val)
{
	if (!current->kunit_test)
		return;

	kunit_info(current->kunit_test, "opl3_command: cmd=%x, val=%x\n", cmd, val);

	fake_opl3_state[cmd] = val;

	if (current_write_ptr) {
		if (current_write_ptr->cmd == 0xFFFF && current_write_ptr->val == 0xFFFF) {
			current_write_ptr = NULL;
			return;
		}
		KUNIT_EXPECT_EQ(current->kunit_test, cmd, current_write_ptr->cmd);
		KUNIT_EXPECT_EQ(current->kunit_test, val, current_write_ptr->val);
		current_write_ptr++;
	}
}


static unsigned char fake_opl3_status(struct snd_opl3 * opl3)
{
	return 0;
}


static void opl3_test_init(struct kunit *test)
{
	struct platform_device *test_device;
	struct snd_card *test_card;
	struct snd_opl3 *test_opl3;

	/* We will expect the following register writes */
	struct expected_write init_writes[] = {
		{ OPL3_REG_TEST, OPL3_ENABLE_WAVE_SELECT },
		{ OPL3_REG_PERCUSSION, 0 },
		{ 0xFFFF, 0xFFFF }
	};

	platform_driver_register(&fake_platform);
	test_device = platform_device_alloc("opl3_test", 0);

	snd_card_new(&test_device->dev, index[0], "fake_opl3", THIS_MODULE, 0, &test_card);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_card);

	strcpy(test_card->driver, "opl3_test");
	strcpy(test_card->shortname, "fake_opl3");
	strcpy(test_card->longname, "fake_opl3 fake card");

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_new(test_card, OPL3_HW_OPL2, &test_opl3));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, test_opl3);


	test_opl3->command = fake_opl3_command;
	test_opl3->status = fake_opl3_status;

	current_write_ptr = init_writes;

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_init(test_opl3));

	current_write_ptr = NULL;

	kunit_info(current->kunit_test, "opl3 card_disconnect");
	snd_card_disconnect(test_card);
	kunit_info(current->kunit_test, "opl3 card_free_When_closed");
	snd_card_free(test_card);

	/* NOTE: We don't need to unregister the platform device separately. */
	kunit_info(current->kunit_test, "opl3 platform unreg");
	platform_driver_unregister(&fake_platform);
}

static void opl3_test_hwdep_new(struct kunit *test)
{
	struct platform_device *test_device;
	struct snd_card *test_card;
	struct snd_opl3 *test_opl3;
	struct snd_hwdep *test_hwdep;

	platform_driver_register(&fake_platform);
	test_device = platform_device_alloc("opl3_test", 0);

	snd_card_new(&test_device->dev, index[0], "fake_opl3", THIS_MODULE, 0, &test_card);

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_new(test_card, OPL3_HW_OPL2, &test_opl3));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, test_opl3);

	test_opl3->command = fake_opl3_command;
	test_opl3->status = fake_opl3_status;

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_init(test_opl3));

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_hwdep_new(test_opl3, 0, 0, &test_hwdep));

	snd_card_disconnect(test_card);
	snd_card_free_when_closed(test_card);
	
	platform_driver_unregister(&fake_platform);
}

static void opl3_test_reset(struct kunit *test)
{
	struct platform_device *test_device;
	struct snd_card *test_card;
	struct snd_opl3 *test_opl3;
	struct snd_hwdep *test_hwdep;

	platform_driver_register(&fake_platform);
	test_device = platform_device_alloc("opl3_test", 0);

	snd_card_new(&test_device->dev, index[0], "fake_opl3", THIS_MODULE, 0, &test_card);

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_new(test_card, OPL3_HW_OPL2, &test_opl3));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, test_opl3);

	test_opl3->command = fake_opl3_command;
	test_opl3->status = fake_opl3_status;

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_init(test_opl3));

	KUNIT_EXPECT_EQ(test, 0, snd_opl3_hwdep_new(test_opl3, 0, 0, &test_hwdep));

	/* poison the state before the reset, so we can verify it */
	memset(fake_opl3_state, 0xFF, sizeof(fake_opl3_state));
	

	snd_opl3_reset(test_opl3);

	/* after a reset: */

	snd_card_disconnect(test_card);
	snd_card_free(test_card);
	
	platform_driver_unregister(&fake_platform);
}

static struct kunit_case opl3_test_cases[] = {
	KUNIT_CASE(opl3_test_init),
	KUNIT_CASE(opl3_test_hwdep_new),
	KUNIT_CASE(opl3_test_reset),
	{},
};

static struct kunit_suite opl3_test_suite = {
	.name = "opl3",
	.test_cases = opl3_test_cases,
};

kunit_test_suites(&opl3_test_suite);

MODULE_LICENSE("GPL v2");
