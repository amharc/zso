#include "v2d_validate.h"

static int v2d_check_pos(uint32_t command,
                         uint32_t pos,
                         uint16_t width,
                         uint16_t height,
                         const struct v2d_context *context,
                         const char *op_name,
                         const char *pos_name) {
    if (unlikely(pos == V2D_INVALID_COMMAND)) {
        printk(KERN_WARNING V2D_PREFIX "%s not preceded by %s\n", op_name, pos_name);
        return -EINVAL;
    }

    if (unlikely(VINTAGE2D_CMD_POS_X(pos) + width > context->width ||
                 VINTAGE2D_CMD_POS_Y(pos) + height > context->height)) {
        printk(KERN_WARNING V2D_PREFIX "illegal %s command: %08x, dst_pos = "
                                       "(%u,%u), (w,h) = (%u,%u), but context = "
                                       "(%u,%u)\n",
               op_name, (unsigned)command, (unsigned)VINTAGE2D_CMD_POS_X(pos), (unsigned)VINTAGE2D_CMD_POS_Y(pos),
               (unsigned)width, (unsigned)height, (unsigned)context->width, (unsigned)context->height);
        return -EINVAL;
    }

    return 0;
}

#define V2D_VALIDATE_SET_POS(state, context, cmd, MACRO, name, member)                                                 \
    {                                                                                                                  \
        uint16_t x = VINTAGE2D_CMD_POS_X(cmd);                                                                         \
        uint16_t y = VINTAGE2D_CMD_POS_Y(cmd);                                                                         \
        if (unlikely(cmd != MACRO(x, y, 0))) {                                                                         \
            printk(KERN_WARNING V2D_PREFIX "illegal " name " command: %08x\n", (unsigned)cmd);                         \
            return -EINVAL;                                                                                            \
        }                                                                                                              \
        if (unlikely(x >= context->width || y >= context->height)) {                                                   \
            printk(KERN_WARNING V2D_PREFIX "illegal " name " command: %08x, setting "                                  \
                                           "(x,y) to (%u,%u), but (w,h) is (%u,%u)\n",                                 \
                   (unsigned)cmd, (unsigned)x, (unsigned)y, (unsigned)context->width, (unsigned)context->height);      \
            return -EINVAL;                                                                                            \
        }                                                                                                              \
        state->member = cmd;                                                                                           \
        return 0;                                                                                                      \
    }

static int v2d_validate_src_pos(struct v2d_state *state, const struct v2d_context *context, uint32_t cmd) {
    V2D_VALIDATE_SET_POS(state, context, cmd, VINTAGE2D_CMD_SRC_POS, "SRC_POS", src_pos_cmd);
}

static int v2d_validate_dst_pos(struct v2d_state *state, const struct v2d_context *context, uint32_t cmd) {
    V2D_VALIDATE_SET_POS(state, context, cmd, VINTAGE2D_CMD_DST_POS, "DST_POS", dst_pos_cmd);
}

static int v2d_validate_fill_color(struct v2d_state *state, const struct v2d_context *context, uint32_t cmd) {
    uint8_t color = VINTAGE2D_CMD_COLOR(cmd);
    if (unlikely(cmd != VINTAGE2D_CMD_FILL_COLOR(color, 0))) {
        printk(KERN_WARNING V2D_PREFIX "illegal FILL_COLOR command: %08x\n", (unsigned)cmd);
        return -EINVAL;
    }
    state->fill_color_cmd = cmd;
    return 0;
}

static int v2d_validate_do_fill(struct v2d_state *state, const struct v2d_context *context, uint32_t cmd) {
    uint16_t w = VINTAGE2D_CMD_WIDTH(cmd);
    uint16_t h = VINTAGE2D_CMD_HEIGHT(cmd);
    int ret;

    if (unlikely(cmd != VINTAGE2D_CMD_DO_FILL(w, h, 0))) {
        printk(KERN_WARNING V2D_PREFIX "illegal DO_FILL command: %08x\n", (unsigned)cmd);
        return -EINVAL;
    }

    ret = v2d_check_pos(cmd, state->dst_pos_cmd, w, h, context, "DO_FILL", "DST_POS");
    if (unlikely(IS_ERR_VALUE(ret))) {
        return ret;
    }

    if (state->fill_color_cmd == V2D_INVALID_COMMAND) {
        printk(KERN_WARNING V2D_PREFIX "DO_FILL not preceded by FILL_COLOR\n");
        return -EINVAL;
    }

    state->dst_pos_cmd = V2D_INVALID_COMMAND;
    state->fill_color_cmd = V2D_INVALID_COMMAND;
    return 0;
}

static int v2d_validate_do_blit(struct v2d_state *state, const struct v2d_context *context, uint32_t cmd) {
    uint16_t w = VINTAGE2D_CMD_WIDTH(cmd);
    uint16_t h = VINTAGE2D_CMD_HEIGHT(cmd);
    int ret;

    if (unlikely(cmd != VINTAGE2D_CMD_DO_BLIT(w, h, 0))) {
        printk(KERN_WARNING V2D_PREFIX "illegal DO_BLIT command: %08x\n", (unsigned)cmd);
        return -EINVAL;
    }

    ret = v2d_check_pos(cmd, state->dst_pos_cmd, w, h, context, "DO_BLIT", "DST_POS");
    if (unlikely(IS_ERR_VALUE(ret))) {
        return ret;
    }

    ret = v2d_check_pos(cmd, state->src_pos_cmd, w, h, context, "DO_BLIT", "SRC_POS");
    if (unlikely(IS_ERR_VALUE(ret))) {
        return ret;
    }

    state->dst_pos_cmd = V2D_INVALID_COMMAND;
    state->src_pos_cmd = V2D_INVALID_COMMAND;
    return 0;
}

int v2d_validate(struct v2d_state *state, const struct v2d_context *context, uint32_t cmd) {
    switch (VINTAGE2D_CMD_TYPE(cmd)) {
    case VINTAGE2D_CMD_TYPE_SRC_POS:
        return v2d_validate_src_pos(state, context, cmd);
    case VINTAGE2D_CMD_TYPE_DST_POS:
        return v2d_validate_dst_pos(state, context, cmd);
    case VINTAGE2D_CMD_TYPE_FILL_COLOR:
        return v2d_validate_fill_color(state, context, cmd);
    case VINTAGE2D_CMD_TYPE_DO_FILL:
        return v2d_validate_do_fill(state, context, cmd);
    case VINTAGE2D_CMD_TYPE_DO_BLIT:
        return v2d_validate_do_blit(state, context, cmd);
    default:
        printk(KERN_WARNING V2D_PREFIX "illegal command: %08x\n", (unsigned)cmd);
        return -EINVAL;
    }
}
