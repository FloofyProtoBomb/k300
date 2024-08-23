#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/artificial_neural_network.h>
#include "sched.h"

#define SCHED_MIGRATION_DEBUG	0

#define NEURAL_NETWORK_NORMALIZATION_FACTOR	10000
#define LEARNING_RATE	13
#define DEFAULT_ERROR	7
#define SYNAPSE_MINIMUM_VALUE	503
#define SYNAPSE_MAXIMUM_VALUE	5003

#define NEURAL_NETWORK_LAYER_NUMBER	3

#define NEURAL_NETWORK_0_LAYER_NEURON_NUMBER	4
#define NEURAL_NETWORK_1_LAYER_NEURON_NUMBER	5
#define NEURAL_NETWORK_2_LAYER_NEURON_NUMBER	1

static struct neural_network* sched_migration_cost_policy_nn;

static int layer0_synapses[NEURAL_NETWORK_0_LAYER_NEURON_NUMBER *
	NEURAL_NETWORK_1_LAYER_NEURON_NUMBER] =
	{ 1400, 1410, 1420, 1430, 1440,
	1100, 1110, 1120, 1130, 1140,
	1000, 1010, 1020, 1030, 1040,
	1350, 1360, 1370, 1380, 1390 };

static int layer1_synapses[NEURAL_NETWORK_1_LAYER_NEURON_NUMBER *
	NEURAL_NETWORK_2_LAYER_NEURON_NUMBER] =
	{ 1010, 1050, 1200, 1150, 990 };

static int ReLU(int value)
{
	if (value < 0)
		return 0;
	else
		return value;
}

int make_sched_migration_cost_pnn(void)
{
	if (sched_migration_cost_policy_nn)
		return 0;

	sched_migration_cost_policy_nn = make_neural_network(
		NEURAL_NETWORK_NORMALIZATION_FACTOR, ReLU, LEARNING_RATE,
		SYNAPSE_MINIMUM_VALUE, SYNAPSE_MAXIMUM_VALUE);

	set_layer_number(sched_migration_cost_policy_nn,
		NEURAL_NETWORK_LAYER_NUMBER);

	set_layer_neuron_number(sched_migration_cost_policy_nn, 0,
		NEURAL_NETWORK_0_LAYER_NEURON_NUMBER);
	set_layer_neuron_number(sched_migration_cost_policy_nn, 1,
		NEURAL_NETWORK_1_LAYER_NEURON_NUMBER);
	set_layer_neuron_number(sched_migration_cost_policy_nn, 2,
		NEURAL_NETWORK_2_LAYER_NEURON_NUMBER);

	set_layer_synapse_weight(sched_migration_cost_policy_nn, 0,
		NEURAL_NETWORK_0_LAYER_NEURON_NUMBER,
		NEURAL_NETWORK_1_LAYER_NEURON_NUMBER, layer0_synapses);
	set_layer_synapse_weight(sched_migration_cost_policy_nn, 1,
		NEURAL_NETWORK_1_LAYER_NEURON_NUMBER,
		NEURAL_NETWORK_2_LAYER_NEURON_NUMBER, layer1_synapses);

	return 0;
}

#define OUTPUT_POLICY_THRESHOLD	2000

#define INITIAL_POLICY_SCHED_MIGRATION_COST	(0)
#define MAX_POLICY_SCHED_MIGRATION_COST	(300000)
#define MIN_POLICY_SCHED_MIGRATION_COST	(-190000)
#define POLICY_SCHED_MIGRATION_COST_GRADIENT	(17027)
int policy_sched_migration_cost;

#define SCHED_DOMAIN_NUMBER	2
#define FIRST_CPU_OF_SECOND_SCHED_DOMAIN	4
#define CPU_NUMBER_OF_SECOND_SCHED_DOMAIN	4

#define LOAD_VARIANCE_NUMBER	64
unsigned int load_buffer[NR_CPUS];

static unsigned int load_variance[SCHED_DOMAIN_NUMBER][LOAD_VARIANCE_NUMBER];
static unsigned int load_index;

unsigned int idle_balance_try_count;
unsigned int idle_balance_reject_count;
unsigned int idle_balance_count;
static unsigned int load_variance_average[SCHED_DOMAIN_NUMBER];

static unsigned int prev_idle_balance_try_count;
static unsigned int prev_idle_balance_reject_count;
static unsigned int prev_idle_balance_count;
static unsigned int prev_load_variance_average[SCHED_DOMAIN_NUMBER];

#define RUN_POLICY_GRADIENT_PERIOD	80

#define MAX_LOAD_VARIANCE_AVERAGE	\
	(INT_MAX / (SYNAPSE_MAXIMUM_VALUE * NEURAL_NETWORK_1_LAYER_NEURON_NUMBER))

struct task_struct* sched_migration_cost_pg;

static inline void increase_policy_sched_migration_cost(void)
{
	policy_sched_migration_cost += POLICY_SCHED_MIGRATION_COST_GRADIENT;
	if (MAX_POLICY_SCHED_MIGRATION_COST < policy_sched_migration_cost)
		policy_sched_migration_cost = MAX_POLICY_SCHED_MIGRATION_COST;
}

static inline void decrease_policy_sched_migration_cost(void)
{
	policy_sched_migration_cost -= POLICY_SCHED_MIGRATION_COST_GRADIENT;
	if (policy_sched_migration_cost < MIN_POLICY_SCHED_MIGRATION_COST)
		policy_sched_migration_cost = MIN_POLICY_SCHED_MIGRATION_COST;
}

static int run_policy_neural_network(void)
{
	int input_values[NEURAL_NETWORK_0_LAYER_NEURON_NUMBER];
	int output_value_number;
	int output_values[NEURAL_NETWORK_2_LAYER_NEURON_NUMBER];

	int prev_input_values[NEURAL_NETWORK_0_LAYER_NEURON_NUMBER];
	int prev_output_value_number;
	int prev_output_values[NEURAL_NETWORK_2_LAYER_NEURON_NUMBER];

	int errors[NEURAL_NETWORK_2_LAYER_NEURON_NUMBER];

	int i;
	int prev_val;
	int cur_val;

	for (i = 0; i < SCHED_DOMAIN_NUMBER; ++i) {
		if (MAX_LOAD_VARIANCE_AVERAGE < load_variance_average[i])
			load_variance_average[i] = MAX_LOAD_VARIANCE_AVERAGE;
	}

	input_values[0] = idle_balance_try_count;
	input_values[1] = idle_balance_count;
	input_values[2] = load_variance_average[0];
	input_values[3] = load_variance_average[1];

	if ((RUN_POLICY_GRADIENT_PERIOD * LOAD_VARIANCE_NUMBER <=
		idle_balance_try_count) &&
		idle_balance_reject_count == 0) {
		set_input_values(sched_migration_cost_policy_nn,
			NEURAL_NETWORK_0_LAYER_NEURON_NUMBER, input_values);
		run_neural_network(sched_migration_cost_policy_nn);
		get_output_values(sched_migration_cost_policy_nn,
			&output_value_number, output_values);

#if SCHED_MIGRATION_DEBUG
		print_neural_network(sched_migration_cost_policy_nn);
#endif

		errors[0] = DEFAULT_ERROR;

		backpropagation_of_error(sched_migration_cost_policy_nn,
			output_value_number, errors);

		increase_policy_sched_migration_cost();
	} else if ((RUN_POLICY_GRADIENT_PERIOD * LOAD_VARIANCE_NUMBER <=
		idle_balance_try_count) && idle_balance_count == 0) {
		set_input_values(sched_migration_cost_policy_nn,
			NEURAL_NETWORK_0_LAYER_NEURON_NUMBER, input_values);
		run_neural_network(sched_migration_cost_policy_nn);
		get_output_values(sched_migration_cost_policy_nn,
			&output_value_number, output_values);

#if SCHED_MIGRATION_DEBUG
		print_neural_network(sched_migration_cost_policy_nn);
#endif

		errors[0] = -DEFAULT_ERROR;

		backpropagation_of_error(sched_migration_cost_policy_nn,
			output_value_number, errors);

		decrease_policy_sched_migration_cost();
	} else {
		prev_input_values[0] = prev_idle_balance_try_count;
		prev_input_values[1] = prev_idle_balance_count;
		prev_input_values[2] = prev_load_variance_average[0];
		prev_input_values[3] = prev_load_variance_average[1];

		set_input_values(sched_migration_cost_policy_nn,
			NEURAL_NETWORK_0_LAYER_NEURON_NUMBER, prev_input_values);
		run_neural_network(sched_migration_cost_policy_nn);
		get_output_values(sched_migration_cost_policy_nn,
			&prev_output_value_number, prev_output_values);

		prev_val = (prev_input_values[2] + prev_input_values[3]) *
			RUN_POLICY_GRADIENT_PERIOD /
			(prev_input_values[0] + prev_input_values[1]);
		cur_val = (input_values[2] + input_values[3]) *
			RUN_POLICY_GRADIENT_PERIOD /
			(input_values[0] + input_values[1]);

		if (OUTPUT_POLICY_THRESHOLD < prev_output_values[0]) {
			/* decrease sched_migration_cost */
			if (cur_val < prev_val) {
				/* positive feedback */
				errors[0] = (cur_val - prev_val) / 37;
				backpropagation_of_error(sched_migration_cost_policy_nn,
					prev_output_value_number, errors);
			} else if (prev_val < cur_val) {
				/* negative feedback */
				errors[0] = (cur_val - prev_val) / 47;
				backpropagation_of_error(sched_migration_cost_policy_nn,
					prev_output_value_number, errors);
			}
		} else {
			/* increase sched_migration_cost */
			if (cur_val < prev_val) {
				/* positive feedback */
				errors[0] = DEFAULT_ERROR;
				backpropagation_of_error(sched_migration_cost_policy_nn,
					prev_output_value_number, errors);
			} else if (prev_val < cur_val) {
				/* negative feedback */
				errors[0] = (prev_val - cur_val) / 29;
				backpropagation_of_error(sched_migration_cost_policy_nn,
					prev_output_value_number, errors);
			}
		}

		set_input_values(sched_migration_cost_policy_nn,
			NEURAL_NETWORK_0_LAYER_NEURON_NUMBER, input_values);
		run_neural_network(sched_migration_cost_policy_nn);
		get_output_values(sched_migration_cost_policy_nn,
			&output_value_number, output_values);

#if SCHED_MIGRATION_DEBUG
		print_neural_network(sched_migration_cost_policy_nn);
#endif

		if (OUTPUT_POLICY_THRESHOLD < output_values[0])
			decrease_policy_sched_migration_cost();
		else
			increase_policy_sched_migration_cost();
	}

	return 0;
}

static void store_input_values(void)
{
	int i;

	prev_idle_balance_try_count = idle_balance_try_count;
	prev_idle_balance_reject_count = idle_balance_reject_count;
	prev_idle_balance_count = idle_balance_count;

	for (i = 0; i < SCHED_DOMAIN_NUMBER; ++i)
		prev_load_variance_average[i] = load_variance_average[i];
}

static void initialize_input_values(void)
{
	int i;
	int j;

	idle_balance_try_count = 0;
	idle_balance_reject_count = 0;
	idle_balance_count = 0;

	for (i = 0; i < SCHED_DOMAIN_NUMBER; ++i)
		load_variance_average[i] = 0;

	load_index = 0;

	for (i = 0; i < SCHED_DOMAIN_NUMBER; ++i) {
		for (j = 0; j < LOAD_VARIANCE_NUMBER; ++j)
			load_variance[i][j] = 0;
	}
}

int sched_migration_cost_pg_thread(void *unused)
{
	int i;
	int load_average;

	while (1) {
		msleep(RUN_POLICY_GRADIENT_PERIOD);

		get_cur_cpu_load();

		load_average = 0;
		for (i = 0; i < FIRST_CPU_OF_SECOND_SCHED_DOMAIN; ++i)
			load_average += load_buffer[i];

		load_average /= FIRST_CPU_OF_SECOND_SCHED_DOMAIN;

		load_variance[0][load_index] = 0;
		for (i = 0; i < FIRST_CPU_OF_SECOND_SCHED_DOMAIN; ++i)
			load_variance[0][load_index] +=
				((load_average - load_buffer[i]) *
				(load_average - load_buffer[i]));

		load_average = 0;
		for (i = FIRST_CPU_OF_SECOND_SCHED_DOMAIN;
			i < FIRST_CPU_OF_SECOND_SCHED_DOMAIN +
			CPU_NUMBER_OF_SECOND_SCHED_DOMAIN; ++i)
			load_average += load_buffer[i];

		load_average /= CPU_NUMBER_OF_SECOND_SCHED_DOMAIN;

		load_variance[1][load_index] = 0;
		for (i = FIRST_CPU_OF_SECOND_SCHED_DOMAIN;
			i < FIRST_CPU_OF_SECOND_SCHED_DOMAIN +
			CPU_NUMBER_OF_SECOND_SCHED_DOMAIN; ++i)
			load_variance[1][load_index] +=
				((load_average - load_buffer[i]) *
				(load_average - load_buffer[i]));

		++load_index;

		if (load_index == LOAD_VARIANCE_NUMBER) {
			load_variance_average[0] = 0;
			for (i = 0; i < LOAD_VARIANCE_NUMBER; ++i)
				load_variance_average[0] += load_variance[0][i];

			load_variance_average[0] /= LOAD_VARIANCE_NUMBER;

			load_variance_average[1] = 0;
			for (i = 0; i < LOAD_VARIANCE_NUMBER; ++i)
				load_variance_average[1] += load_variance[1][i];

			load_variance_average[1] /= LOAD_VARIANCE_NUMBER;

			run_policy_neural_network();

#if SCHED_MIGRATION_DEBUG
			pr_err("[sched_migration_cost_pnn] %u %u %u %u %u = %d\n",
				idle_balance_try_count,
				idle_balance_reject_count,
				idle_balance_count,
				load_variance_average[0],
				load_variance_average[1],
				policy_sched_migration_cost);
#endif

			store_input_values();
			initialize_input_values();
		}
	}

	return 0;
}

static int __init init_sched_migration_cost_pg(void)
{
    make_sched_migration_cost_pnn();

	policy_sched_migration_cost = INITIAL_POLICY_SCHED_MIGRATION_COST;

    sched_migration_cost_pg =
        kthread_run(sched_migration_cost_pg_thread, 0,
			"sched_migration_cost_pg");
    set_user_nice(sched_migration_cost_pg, MAX_NICE);

    return 0;
}
fs_initcall(init_sched_migration_cost_pg);
