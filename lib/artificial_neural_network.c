#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/artificial_neural_network.h>

struct neural_network*
make_neural_network(int normalization_factor, int (*activation_function)(int),
	int learning_rate, int synapse_minimum_value, int synapse_maximum_value)
{
	struct neural_network* nn;

	nn = (struct neural_network*)kzalloc(sizeof(struct neural_network), GFP_KERNEL);
	nn->normalization_factor = normalization_factor;
	nn->activation_function = activation_function;
	nn->learning_rate = learning_rate;
	nn->synapse_minimum_value = synapse_minimum_value;
	nn->synapse_maximum_value = synapse_maximum_value;

	return nn;
}

int set_layer_number(struct neural_network* nn, int number)
{
	nn->layer_number = number;

	nn->layer_neurons = (struct layer_neuron*)kzalloc(
		sizeof(struct layer_neuron) * nn->layer_number, GFP_KERNEL);
	nn->layer_synapses = (struct layer_synapse*)kzalloc(
		sizeof(struct layer_synapse) * nn->layer_number, GFP_KERNEL);

	return 0;
}

int set_layer_neuron_number(struct neural_network* nn,
	int layer_number, int neuron_number)
{
	struct layer_neuron* layer_neuron;
	struct layer_synapse* layer_synapse;
	int i;

	layer_neuron = &(nn->layer_neurons[layer_number]);
	layer_neuron->neuron_number = neuron_number;
	layer_neuron->neurons = (int*)kzalloc(sizeof(int) *
		layer_neuron->neuron_number, GFP_KERNEL);

	if (0 < layer_number) {
		layer_synapse = &(nn->layer_synapses[layer_number - 1]);
		layer_synapse->output_neuron_number = neuron_number;
		layer_synapse->synapses = (int**)kzalloc(sizeof(int*) *
			layer_synapse->input_neuron_number, GFP_KERNEL);
		for (i = 0; i < layer_synapse->input_neuron_number; ++i)
			layer_synapse->synapses[i] = (int*)kzalloc(sizeof(int) *
				layer_synapse->output_neuron_number, GFP_KERNEL);

		layer_synapse->cached_synapses = (int**)kzalloc(sizeof(int*) *
			layer_synapse->input_neuron_number, GFP_KERNEL);
		for (i = 0; i < layer_synapse->input_neuron_number; ++i)
			layer_synapse->cached_synapses[i] = (int*)kzalloc(sizeof(int) *
				layer_synapse->output_neuron_number, GFP_KERNEL);
	}

	layer_synapse = &(nn->layer_synapses[layer_number]);
	layer_synapse->input_neuron_number = neuron_number;

	return 0;
}

int set_layer_synapse_weight(struct neural_network* nn,
	int layer_number, int input_neuron_number, int output_neuron_number,
	int* weight)
{
	struct layer_synapse* layer_synapse;
	int i;
	int j;

	layer_synapse = &(nn->layer_synapses[layer_number]);
	for (i = 0; i < layer_synapse->input_neuron_number && i < input_neuron_number; ++i) {
		for (j = 0; j < layer_synapse->output_neuron_number && j < output_neuron_number; ++j) {
			if (weight[i * output_neuron_number + j] < nn->synapse_minimum_value)
				weight[i * output_neuron_number + j] = nn->synapse_minimum_value;
			if (nn->synapse_maximum_value < weight[i * output_neuron_number + j])
				weight[i * output_neuron_number + j] = nn->synapse_maximum_value;

			layer_synapse->synapses[i][j] = weight[i * output_neuron_number + j];
		}
	}

	return 0;
}

int clear_neural_network(struct neural_network* nn)
{
	struct layer_neuron* layer_neuron;
	struct layer_synapse* layer_synapse;
	int i;
	int j;

	for (i = 0; i < nn->layer_number - 1; ++i) {
		layer_neuron = &(nn->layer_neurons[i]);
		kfree(layer_neuron->neurons);

		layer_synapse = &(nn->layer_synapses[i]);
		for (j = 0; j < layer_synapse->input_neuron_number; ++j) {
			kfree(layer_synapse->synapses[j]);
			kfree(layer_synapse->cached_synapses[j]);
		}

		kfree(layer_synapse->synapses);
	}

	layer_neuron = &(nn->layer_neurons[nn->layer_number - 1]);
	kfree(layer_neuron->neurons);

	kfree(nn->layer_neurons);
	kfree(nn->layer_synapses);

	kfree(nn);

	return 0;
}

int set_input_values(struct neural_network* nn, int input_neuron_number,
	int* input_values)
{
	struct layer_neuron* input_layer_neuron;
	int i;
	int j;

	input_layer_neuron = &(nn->layer_neurons[0]);
	for (i = 0, j = 0; i < input_layer_neuron->neuron_number &&
		j < input_neuron_number; ++i, ++j)
		input_layer_neuron->neurons[i] = input_values[j];

	return 0;
}

int run_neural_network(struct neural_network* nn)
{
	struct layer_neuron* input_layer_neuron;
	struct layer_neuron* output_layer_neuron;
	struct layer_synapse* layer_synapse;
	int i;
	int j;
	int k;

	for (i = 0; i < nn->layer_number - 1; ++i) {
		input_layer_neuron = &(nn->layer_neurons[i]);
		output_layer_neuron = &(nn->layer_neurons[i + 1]);
		layer_synapse = &(nn->layer_synapses[i]);

		for (j = 0; j < output_layer_neuron->neuron_number; ++j) {
			output_layer_neuron->neurons[j] = 0;
			for (k = 0; k < input_layer_neuron->neuron_number; ++k)
				output_layer_neuron->neurons[j] +=
					input_layer_neuron->neurons[k] *
					layer_synapse->synapses[k][j];

			output_layer_neuron->neurons[j] /= nn->normalization_factor;
			output_layer_neuron->neurons[j] =
				nn->activation_function(output_layer_neuron->neurons[j]);
		}
	}

	return 0;
}

int get_output_values(struct neural_network* nn,
	int* output_value_number, int* output_values)
{
	struct layer_neuron* output_layer_neuron;
	int i;

	output_layer_neuron = &(nn->layer_neurons[nn->layer_number - 1]);
	*output_value_number = output_layer_neuron->neuron_number;

	for (i = 0; i < output_layer_neuron->neuron_number; ++i)
		output_values[i] = output_layer_neuron->neurons[i];

	return 0;
}

int backpropagation_of_error(struct neural_network* nn,
	int output_value_number, int* errors)
{
	struct layer_neuron* input_layer_neuron;
	struct layer_neuron* output_layer_neuron;
	struct layer_synapse* layer_synapse;
	int i;
	int j;
	int k;

	output_layer_neuron = &(nn->layer_neurons[nn->layer_number - 1]);
	for (i = 0; i < output_layer_neuron->neuron_number; ++i)
		output_layer_neuron->neurons[i] = errors[i] * nn->learning_rate;

	for (i = nn->layer_number - 1; 0 < i; --i) {
		output_layer_neuron = &(nn->layer_neurons[i]);
		input_layer_neuron = &(nn->layer_neurons[i - 1]);
		layer_synapse = &(nn->layer_synapses[i - 1]);

		for (j = 0; j < output_layer_neuron->neuron_number; ++j) {
			for (k = 0; k < input_layer_neuron->neuron_number; ++k) {
				layer_synapse->cached_synapses[k][j] =
					layer_synapse->synapses[k][j];
				layer_synapse->synapses[k][j] -=
					(output_layer_neuron->neurons[j] *
					input_layer_neuron->neurons[k]) / nn->normalization_factor;

				layer_synapse->synapses[k][j] -= (j - k);

				if (layer_synapse->synapses[k][j] < nn->synapse_minimum_value)
					layer_synapse->synapses[k][j] = nn->synapse_minimum_value;
				if (nn->synapse_maximum_value < layer_synapse->synapses[k][j])
					layer_synapse->synapses[k][j] = nn->synapse_maximum_value;
			}
		}

		for (j = 0; j < input_layer_neuron->neuron_number; ++j)
			input_layer_neuron->neurons[j] = 0;

		for (j = 0; j < output_layer_neuron->neuron_number; ++j) {
			for (k = 0; k < input_layer_neuron->neuron_number; ++k)
				input_layer_neuron->neurons[k] +=
					(output_layer_neuron->neurons[j] *
					layer_synapse->cached_synapses[k][j]) *
					nn->learning_rate / nn->normalization_factor;
		}
	}

	return 0;
}

int print_neural_network(struct neural_network* nn)
{
	struct layer_neuron* cur_layer_neuron;
	struct layer_synapse* cur_layer_synapse;
	int i;
	int j;
	int k;

	for (i = 0; i < nn->layer_number; ++i) {
		cur_layer_neuron = &(nn->layer_neurons[i]);
		cur_layer_synapse = &(nn->layer_synapses[i]);

		for (j = 0; j < cur_layer_neuron->neuron_number; ++j)
			pr_info("neuron[%d][%d] = %d ", i, j,
				cur_layer_neuron->neurons[j]);

		pr_info("\n");

		for (j = 0; j < cur_layer_synapse->input_neuron_number; ++j) {
			for (k = 0; k < cur_layer_synapse->output_neuron_number; ++k)
				pr_info("synapse[%d][%d][%d] = %d ", i, j, k,
					cur_layer_synapse->synapses[j][k]);

			pr_info("\n");
		}
	}

	return 0;
}
