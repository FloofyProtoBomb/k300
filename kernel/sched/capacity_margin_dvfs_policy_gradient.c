#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/artificial_neural_network.h>
#include "sched.h"

#define CAPACITY_MARGIN_DEBUG	0

#define NEURAL_NETWORK_NORMALIZATION_FACTOR	10000
#define LEARNING_RATE	31
#define DEFAULT_ERROR	7
#define SYNAPSE_MINIMUM_VALUE	503
#define SYNAPSE_MAXIMUM_VALUE	5003

#define NEURAL_NETWORK_LAYER_NUMBER	3

#define NEURAL_NETWORK_0_LAYER_NEURON_NUMBER	4
#define NEURAL_NETWORK_1_LAYER_NEURON_NUMBER	5
#define NEURAL_NETWORK_2_LAYER_NEURON_NUMBER	1

static struct neural_network* capacity_margin_dvfs_policy_nn;

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

int make_capacity_margin_dvfs_pnn(void)
{
	if (capacity_margin_dvfs_policy_nn)
		return 0;

	capacity_margin_dvfs_policy_nn = make_neural_network(
		NEURAL_NETWORK_NORMALIZATION_FACTOR, ReLU, LEARNING_RATE,
		SYNAPSE_MINIMUM_VALUE, SYNAPSE_MAXIMUM_VALUE);

	set_layer_number(capacity_margin_dvfs_policy_nn,
		NEURAL_NETWORK_LAYER_NUMBER);

	set_layer_neuron_number(capacity_margin_dvfs_policy_nn, 0,
		NEURAL_NETWORK_0_LAYER_NEURON_NUMBER);
	set_layer_neuron_number(capacity_margin_dvfs_policy_nn, 1,
		NEURAL_NETWORK_1_LAYER_NEURON_NUMBER);
	set_layer_neuron_number(capacity_margin_dvfs_policy_nn, 2,
		NEURAL_NETWORK_2_LAYER_NEURON_NUMBER);

	set_layer_synapse_weight(capacity_margin_dvfs_policy_nn, 0,
		NEURAL_NETWORK_0_LAYER_NEURON_NUMBER,
		NEURAL_NETWORK_1_LAYER_NEURON_NUMBER, layer0_synapses);
	set_layer_synapse_weight(capacity_margin_dvfs_policy_nn, 1,
		NEURAL_NETWORK_1_LAYER_NEURON_NUMBER,
		NEURAL_NETWORK_2_LAYER_NEURON_NUMBER, layer1_synapses);

	return 0;
}

int clear_capacity_margin_dvfs_pnn(void)
{
	if (capacity_margin_dvfs_policy_nn) {
		clear_neural_network(capacity_margin_dvfs_policy_nn);
		capacity_margin_dvfs_policy_nn = 0;
	}

	return 0;
}

#define OUTPUT_POLICY_THRESHOLD	200

#define INITIAL_POLICY_CAPACITY_MARGIN_DVFS	10000
#define MAX_POLICY_CAPACITY_MARGIN_DVFS	10000
#define MIN_POLICY_CAPACITY_MARGIN_DVFS	8550
#define POLICY_CAPACITY_MARGIN_DVFS_GRADIENT	11
int policy_capacity_margin_dvfs;

#define SCHED_DOMAIN_NUMBER	2
#define FIRST_CPU_OF_SECOND_SCHED_DOMAIN	4
#define CPU_NUMBER_OF_SECOND_SCHED_DOMAIN	4

unsigned int util_buffer[NR_CPUS];
unsigned int max_util;
unsigned int prev_util_buffer[NR_CPUS];
unsigned int prev_max_util;

#define RUN_POLICY_GRADIENT_PERIOD	50
#define TRAINING_PERIOD	7
unsigned int training_count;

struct task_struct* capacity_margin_dvfs_pg;

static inline void increase_policy_capacity_margin_dvfs(void)
{
	policy_capacity_margin_dvfs += POLICY_CAPACITY_MARGIN_DVFS_GRADIENT;
	if (MAX_POLICY_CAPACITY_MARGIN_DVFS < policy_capacity_margin_dvfs)
		policy_capacity_margin_dvfs = MAX_POLICY_CAPACITY_MARGIN_DVFS;
}

static inline void decrease_policy_capacity_margin_dvfs(void)
{
	policy_capacity_margin_dvfs -= POLICY_CAPACITY_MARGIN_DVFS_GRADIENT;
	if (policy_capacity_margin_dvfs < MIN_POLICY_CAPACITY_MARGIN_DVFS)
		policy_capacity_margin_dvfs = MIN_POLICY_CAPACITY_MARGIN_DVFS;
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

	for (i = CPU_NUMBER_OF_SECOND_SCHED_DOMAIN; i < NR_CPUS; ++i) {
		input_values[i - CPU_NUMBER_OF_SECOND_SCHED_DOMAIN] =
			util_buffer[i];
		prev_input_values[i - CPU_NUMBER_OF_SECOND_SCHED_DOMAIN] =
			prev_util_buffer[i];

		if (max_util < util_buffer[i])
			max_util = util_buffer[i];
	}

	if (max_util < 100) {
		training_count = 0;
		return 0;
	} else if (800 < max_util) {
		training_count = 0;
		policy_capacity_margin_dvfs = MAX_POLICY_CAPACITY_MARGIN_DVFS;
		return 0;
	} else {
		++training_count;
	}

	if (TRAINING_PERIOD <= training_count) {
		/* training policy neural network */
		errors[0] = 0;

		set_input_values(capacity_margin_dvfs_policy_nn,
			NEURAL_NETWORK_0_LAYER_NEURON_NUMBER, prev_input_values);
		run_neural_network(capacity_margin_dvfs_policy_nn);
		get_output_values(capacity_margin_dvfs_policy_nn,
			&prev_output_value_number, prev_output_values);

		if (OUTPUT_POLICY_THRESHOLD < prev_output_values[0]) {
			/* positive rewards */
			if (prev_max_util * 125 * policy_capacity_margin_dvfs /
				MAX_POLICY_CAPACITY_MARGIN_DVFS < max_util) {
				errors[0] = -DEFAULT_ERROR;
			} else if (max_util < prev_max_util) {
				/* negative rewards */
				errors[0] = prev_max_util - max_util;
			}
		} else {
			/* positive rewards */
			if (max_util < prev_max_util) {
				errors[0] = DEFAULT_ERROR;
			} else if (prev_max_util * 125 * policy_capacity_margin_dvfs /
				MAX_POLICY_CAPACITY_MARGIN_DVFS < max_util) {
				/* negative rewards */
				errors[0] = prev_max_util - max_util;
			}
		}

		if (errors[0] != 0) {
			backpropagation_of_error(capacity_margin_dvfs_policy_nn,
				prev_output_value_number, errors);
		}

		training_count = 0;
	}

	if (training_count % 2)
		return 0;

	set_input_values(capacity_margin_dvfs_policy_nn,
		NEURAL_NETWORK_0_LAYER_NEURON_NUMBER, input_values);
	run_neural_network(capacity_margin_dvfs_policy_nn);
	get_output_values(capacity_margin_dvfs_policy_nn,
		&output_value_number, output_values);

#if CAPACITY_MARGIN_DEBUG
	print_neural_network(capacity_margin_dvfs_policy_nn);
#endif

	if (OUTPUT_POLICY_THRESHOLD < output_values[0])
		increase_policy_capacity_margin_dvfs();
	else
		decrease_policy_capacity_margin_dvfs();

	return 0;
}

static void store_input_values(void)
{
	int i;

	for (i = 0; i < NR_CPUS; ++i)
		prev_util_buffer[i] = util_buffer[i];

	prev_max_util = max_util;
}

static void initialize_input_values(void)
{
	max_util = 0;
}

int capacity_margin_dvfs_pg_thread(void *unused)
{
	while (1) {
		msleep(RUN_POLICY_GRADIENT_PERIOD);

		if (kthread_should_stop()) {
			pr_debug("capacity_margin_dvfs_pg_thread stop\n");
			return 0;
		}

		run_policy_neural_network();

#if CAPACITY_MARGIN_DEBUG
		pr_info("[capacity_margin_dvfs_pnn] %u %u %u %u = %d\n",
			util_buffer[4], util_buffer[5], util_buffer[6], util_buffer[7],
			policy_capacity_margin_dvfs);
#endif

		store_input_values();
		initialize_input_values();
	}

	return 0;
}

int init_capacity_margin_dvfs_pg(void)
{
	make_capacity_margin_dvfs_pnn();

	if (capacity_margin_dvfs_pg)
		return 0;

	policy_capacity_margin_dvfs = INITIAL_POLICY_CAPACITY_MARGIN_DVFS;

	capacity_margin_dvfs_pg =
		kthread_run(capacity_margin_dvfs_pg_thread, 0,
			"capacity_margin_dvfs_pg");
	set_user_nice(capacity_margin_dvfs_pg, MAX_NICE);

	return 0;
}

int exit_capacity_margin_dvfs_pg(void)
{
	if (capacity_margin_dvfs_pg) {
		kthread_stop(capacity_margin_dvfs_pg);
		capacity_margin_dvfs_pg = 0;
	}

	policy_capacity_margin_dvfs = INITIAL_POLICY_CAPACITY_MARGIN_DVFS;

	clear_capacity_margin_dvfs_pnn();

	return 0;
}
