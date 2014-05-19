#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "clk.h"


/*
 * pll1 --|--\
 *        |M |--[GATE]-[DIV]-
 * pll2 --|--/
 */

struct clk *rockchip_clk_register_composite(const char *name,
		const char **parent_names, u8 num_parents, void __iomem *base,
		int muxdiv_offset, u8 mux_shift, u8 mux_width, u8 mux_flags,
		u8 div_shift, u8 div_width, u8 div_flags, int gate_offset,
		u8 gate_shift, u8 gate_flags, unsigned long flags,
		spinlock_t *lock)
{
	struct clk *clk;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate;
	struct clk_divider *div;

	if (num_parents > 1) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->reg = base + muxdiv_offset;
		mux->shift = mux_shift;
		mux->mask = BIT(mux_width) - 1;
		mux->flags = mux_flags;
		mux->lock = lock;
	}

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->flags = gate_flags;
	gate->reg = base + gate_offset;
	gate->bit_idx = gate_shift;
	gate->lock = lock;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	div->flags = div_flags;
	div->reg = base + muxdiv_offset;
	div->shift = div_shift;
	div->width = div_width;
	div->lock = lock;

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
				     mux ? &mux->hw : NULL, mux ? &clk_mux_ops : NULL,
				     &div->hw, &clk_divider_ops,
				     &gate->hw, &clk_gate_ops,
				     flags);
	if (IS_ERR(clk))
		return clk;

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));
	return clk;
}
