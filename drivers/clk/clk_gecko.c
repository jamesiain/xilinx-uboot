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

#undef EXTRA_DEBUG_OUTPUT

#define LMK_LD_TIMEOUT_MS (3000)

DECLARE_GLOBAL_DATA_PTR;

struct gecko_clk_priv {
    ulong dac_system_clock_freq;
};

static int config_lmk(struct udevice *dev)
{
    uint32_t const lmk_setting[] = {
        0x000080, 0x000000, 0x000200, 0x010002, 0x010155, 0x010300,
        0x010400, 0x010500, 0x0106F1, 0x010703, 0x010804, 0x010955, 0x010B00,
        0x010C00, 0x010D00, 0x010EF1, 0x010F33, 0x011004, 0x011155, 0x011300,
        0x011402, 0x011500, 0x0116F1, 0x011733, 0x011819, 0x011955, 0x011B00,
        0x011C02, 0x011D00, 0x011EF1, 0x011F33, 0x012019, 0x012155, 0x012300,
        0x012402, 0x012500, 0x0126F1, 0x012733, 0x012819, 0x012955, 0x012B00,
        0x012C02, 0x012D00, 0x012EF1, 0x012F03, 0x013002, 0x013155, 0x013300,
        0x013402, 0x013500, 0x0136F1, 0x013703, 0x013820, 0x013900, 0x013A00,
        0x013B32, 0x013C00, 0x013D10, 0x013E03, 0x013F00, 0x01400F, 0x014100,
        0x014200, 0x014313, 0x014400, 0x01457F, 0x014608, 0x01470A, 0x014802,
        0x014942, 0x014A02, 0x014B16, 0x014C00, 0x014D00, 0x014EC0, 0x014F7F,
        0x015003, 0x015102, 0x015200, 0x015300, 0x0154C0, 0x015500, 0x0156C0,
        0x015700, 0x015896, 0x015900, 0x015A7D, 0x015BD4, 0x015C20, 0x015D00,
        0x015E00, 0x015F0B, 0x016000, 0x016102, 0x016240, 0x016300, 0x016400,
        0x016519, 0x01741E, 0x017C15, 0x017D33, 0x016600, 0x016700, 0x016819,
        0x016959, 0x016A20, 0x016B00, 0x016C00, 0x016D00, 0x016E13, 0x017300,
        0x1FFD00, 0x1FFE00, 0x1FFF53,
    };  // Default DAC System Clock is 250 MHz; lmk_setting[46] = 0x013002
        // To configure it to be 125 MHz;  Make lmk_setting[46] = 0x013004
    int const length = sizeof(lmk_setting) / sizeof(uint32_t);

    struct gecko_clk_priv *priv = dev_get_priv(dev);
    int                    index, claim_err;
    bool                   ld1, ld2;
    struct gpio_desc       clk_gen_rst, clk_gen_ld1, clk_gen_ld2;

    if (gpio_request_by_name(dev, "clk-gen-rst", 0, &clk_gen_rst, GPIOD_IS_OUT) < 0) {
        printf("config_lmk: Failed to request GPIO by name: clk-gen-rst\n");
        return -1;
    }

    dm_gpio_set_value(&clk_gen_rst, 1);
    udelay(5);
    dm_gpio_set_value(&clk_gen_rst, 0);

    printf("config_lmk: Clock Reset = %d\n", dm_gpio_get_value(&clk_gen_rst));

    if (claim_err = dm_spi_claim_bus(dev)) {
        printf("config_lmk: Failed to claim SPI bus: %i\n", claim_err);
        return -1;
    }

    for (index = 0; index < length; index++) {
        uint32_t const reg = lmk_setting[index];

        uint8_t dout[3] = {
            (uint8_t)((reg >> 16) & 0xFF),
            (uint8_t)((reg >>  8) & 0xFF),
            (uint8_t)((reg >>  0) & 0xFF),
        };

#ifdef EXTRA_DEBUG_OUTPUT
        fprintf(stderr, "config_lmk: reg[%.3i] = 0x%.8x\n", index, reg);
#endif

        dm_spi_xfer(dev, 24, dout, NULL, SPI_XFER_ONCE);
    }

    dm_spi_release_bus(dev);

    if (gpio_request_by_name(dev, "clk-gen-ld1", 0, &clk_gen_ld1, GPIOD_IS_IN) < 0) {
        printf("config_lmk: Failed to request GPIO by name: clk-gen-ld1\n");
        return -1;
    }

    if (gpio_request_by_name(dev, "clk-gen-ld2", 0, &clk_gen_ld2, GPIOD_IS_IN) < 0) {
        printf("config_lmk: Failed to request GPIO by name: clk-gen-ld2\n");
        return -1;
    }

    for (index = 0, ld1 = false, ld2 = false; index < LMK_LD_TIMEOUT_MS && !(ld1 && ld2); index++) {
        udelay(1000);

        ld1 = dm_gpio_get_value(&clk_gen_ld1);
        ld2 = dm_gpio_get_value(&clk_gen_ld2);
    }

    printf("config_lmk: Clock Lock Detect 1 = %s\n", ld1 ? "Locked" : "Unlocked");
    printf("config_lmk: Clock Lock Detect 2 = %s\n", ld2 ? "Locked" : "Unlocked");

    priv->dac_system_clock_freq = !(ld1 && ld2) ? 0 : 250 * 1000 * 1000;

    return 0;
}

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
    struct gecko_clk_priv *priv = dev_get_priv(dev);

    config_lmk(dev);

    printf("gecko_clk_probe: DAC System Clock = %lu Hz\n", priv->dac_system_clock_freq);
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
