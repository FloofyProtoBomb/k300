#ifndef __LINUX_ARTIFICIAL_NEURAL_NETWORK_H__
#define __LINUX_ARTIFICIAL_NEURAL_NETWORK_H__

struct layer_neuron {
	int neuron_number;
	int* neurons;
};

struct layer_synapse {
	int input_neuron_number;
	int output_neuron_number;
	int** synapses;
	int** cached_synapses;
};

struct neural_network {
	int layer_number;
	struct layer_neuron* layer_neurons;
	struct layer_synapse* layer_synapses;
	int normalization_factor;
	int (*activation_function)(int);
	int learning_rate;
	int synapse_minimum_value;
	int synapse_maximum_value;
};

/* APIs to make artificial neural network */
extern struct neural_network*
make_neural_network(int normalization_factor, int (*activation_function)(int),
	int learning_rate, int synapse_minimum_value, int synapse_maximum_value);
extern int set_layer_number(struct neural_network* nn, int number);
extern int set_layer_neuron_number(struct neural_network* nn,
	int layer_number, int neuron_number);
extern int set_layer_synapse_weight(struct neural_network* nn,
	int layer_number, int input_neuron_number, int output_neuron_number,
	int* weight);
extern int clear_neural_network(struct neural_network* nn);

/* APIs to run artificial neural network */
int set_input_values(struct neural_network* nn, int input_neuron_number,
	int* input_values);
extern int run_neural_network(struct neural_network* nn);
extern int get_output_values(struct neural_network* nn,
	int* output_value_number, int* output_values);

/* APIs to trainning artificial neural network */
extern int backpropagation_of_error(struct neural_network* nn,
	int output_value_number, int* errors);

/* APIs to print values of neurons and synapses */
extern int print_neural_network(struct neural_network* nn);

#endif