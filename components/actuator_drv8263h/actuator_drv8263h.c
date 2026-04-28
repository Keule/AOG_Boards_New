#include "actuator_drv8263h.h"

#include <string.h>

#include "hal_gpio.h"

void actuator_drv8263h_init(actuator_drv8263h_t* act, steering_control_t* control, safety_failsafe_t* safety, uint8_t pwm_pin, uint8_t dir_pin, uint8_t failsafe_pin, bool simulated)
{
    if (act == NULL) {
        return;
    }

    memset(act, 0, sizeof(*act));
    act->component.name = "actuator_drv8263h";
    act->component.user_data = act;
    act->component.service_step = actuator_drv8263h_service_step;
    act->control = control;
    act->safety = safety;
    act->pwm_pin = pwm_pin;
    act->dir_pin = dir_pin;
    act->failsafe_pin = failsafe_pin;
    act->simulated = simulated;

    (void)hal_gpio_set_mode(pwm_pin, HAL_GPIO_MODE_OUTPUT);
    (void)hal_gpio_set_mode(dir_pin, HAL_GPIO_MODE_OUTPUT);
    (void)hal_gpio_set_mode(failsafe_pin, HAL_GPIO_MODE_OUTPUT);
}

void actuator_drv8263h_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    actuator_drv8263h_t* act;
    const steering_command_t* cmd;
    (void)timestamp_us;

    if (comp == NULL || comp->user_data == NULL) {
        return;
    }

    act = (actuator_drv8263h_t*)comp->user_data;
    cmd = steering_control_get_command(act->control);

    if (safety_failsafe_is_active(act->safety) || safety_failsafe_outputs_high_z(act->safety) || cmd == NULL || !cmd->valid) {
        (void)hal_gpio_set(act->failsafe_pin, HAL_GPIO_HIGH);
        return;
    }

    (void)hal_gpio_set(act->failsafe_pin, HAL_GPIO_LOW);
    (void)hal_gpio_set(act->dir_pin, cmd->dir_right ? HAL_GPIO_HIGH : HAL_GPIO_LOW);
    (void)hal_gpio_set(act->pwm_pin, (cmd->pwm > 0.1f) ? HAL_GPIO_HIGH : HAL_GPIO_LOW);
}
