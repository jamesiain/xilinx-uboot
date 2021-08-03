#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <dm/lists.h>
#include <errno.h>
#include <spi.h>
#include <asm/io.h>
#include <asm-generic/gpio.h>
#include <asm/arch/clk.h>
#include <asm/arch/hardware.h>
#include <asm/arch/sys_proto.h>

DECLARE_GLOBAL_DATA_PTR;

struct gecko_clk_priv {
    ulong dac_system_clock_freq;
};

static ulong gecko_clk_get_rate(struct clk *clk)
{
    struct gecko_clk_priv *priv = dev_get_priv(clk->dev);

    return priv->dac_system_clock_freq;
}

static struct clk_ops gecko_clk_ops = {
    .get_rate = gecko_clk_get_rate,
};

static int gecko_clk_probe(struct udevice *dev)
{
    struct gecko_clk_priv *priv  = dev_get_priv(dev);
    struct spi_slave      *slave = spi_setup_slave(0, 0, 500000, 0);
    int                    claim_err;
    struct gpio_desc       clk_gen_cs, clk_gen_ld1, clk_gen_ld2;
    uint8_t                dout[3] = {0x01, 0x30, 0x02};

    if (!slave) {
        printf("gecko_clk_probe: Failed to setup SPI slave\n");
        return -1;
    }

    if (claim_err = spi_claim_bus(slave)) {
        printf("gecko_clk_probe: Failed to claim SPI bus: %i\n", claim_err);
        return -1;
    }

    if (gpio_request_by_name(dev, "clk-gen-cs", 0, &clk_gen_cs, GPIOD_IS_OUT) < 0) {
        printf("Could not request GPIO by name: clk-gen-cs\n");
        return;
    }

    dm_gpio_set_value(&clk_gen_cs, 1);
    udelay(200);
    dm_gpio_set_value(&clk_gen_cs, 0);

    spi_xfer(slave, 24, dout, dout, SPI_XFER_BEGIN | SPI_XFER_END);

    dm_gpio_set_value(&clk_gen_cs, 1);

    priv->dac_system_clock_freq = 250 * 1000 * 1000;

    printf("gecko_clk_probe: DAC System Clock = %lu Hz\n", priv->dac_system_clock_freq);

    if (gpio_request_by_name(dev, "clk-gen-ld1", 0, &clk_gen_ld1, GPIOD_IS_IN) < 0) {
        printf("Could not request GPIO by name: clk-gen-ld1\n");
        return;
    }

    if (gpio_request_by_name(dev, "clk-gen-ld2", 0, &clk_gen_ld2, GPIOD_IS_IN) < 0) {
        printf("Could not request GPIO by name: clk-gen-ld2\n");
        return;
    }

    printf("gecko_clk_probe: Clock Lock Detect 1 = %s\n",
            dm_gpio_get_value(&clk_gen_ld1) ? "Locked" : "Unlocked");

    printf("gecko_clk_probe: Clock Lock Detect 2 = %s\n",
            dm_gpio_get_value(&clk_gen_ld2) ? "Locked" : "Unlocked");

    return 0;
}

static const struct udevice_id gecko_clk_ids[] = {
    { .compatible = "lmk,lmk04821"},
    {}
};

U_BOOT_DRIVER(gecko_clk) = {
    .name                 = "gecko_clk",
    .id                   = UCLASS_CLK,
    .of_match             = gecko_clk_ids,
    .ops                  = &gecko_clk_ops,
    .priv_auto_alloc_size = sizeof(struct gecko_clk_priv),
    .probe                = gecko_clk_probe,
};
