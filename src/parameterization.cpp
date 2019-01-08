//
// Created by krab1k on 20.11.18.
//

#include <vector>
#include <iostream>
#include <iomanip>
#include <set>
#include <nlopt.hpp>

#include "parameterization.h"
#include "parameters.h"
#include "charges.h"
#include "statistics.h"
#include "utility/utility.h"
#include "external/lhs/latin_random.hpp"


double f(const std::vector<double> &x, std::vector<double> &, void *data) {
    auto parameterization = reinterpret_cast<Parameterization *>(data);

    parameterization->method()->parameters()->set_from_vector(x);

    auto charges = Charges();
    for (const auto &molecule: parameterization->set().molecules()) {
        charges.insert(molecule.name(), parameterization->method()->calculate_charges(molecule));
    }

    return RMSD(parameterization->reference_charges(), charges);
}


void Parameterization::parametrize() {

    auto values = method_->parameters()->get_vector();
    size_t n = values.size();

    nlopt::opt opt(nlopt::LN_NEWUOA, n);

    size_t n_points = 100;

    const double lower_bound = 0;
    const double upper_bound = 3;

    std::vector<std::vector<double>> initial(n_points, std::vector<double>(n, 0));
    int seed = 1;
    auto random_sample = latin_random_new(n, n_points, seed);
    for (size_t i = 0; i < n_points; i++) {
        initial[i].assign(random_sample + i * n, random_sample + (i + 1) * n);
        for (auto &x: initial[i]) {
            x = lower_bound + (upper_bound - lower_bound) * x;
        }
    }

    delete[] random_sample;

    std::vector<double> results(n_points, -1);
    std::vector<double> grad;

    for (size_t i = 0; i < n_points; i++) {
        results[i] = f(initial[i], grad, reinterpret_cast<void *>(this));
    }

    auto best_idx = std::min_element(results.begin(), results.end()) - results.begin();
    auto x = initial[best_idx];

    opt.set_min_objective(f, reinterpret_cast<void *>(this));
    opt.set_maxeval(500);

    double minf;
    try {
        opt.optimize(x, minf);
        std::cout << std::setprecision(3) << "Best RMSD = " << minf << std::endl;
    }
    catch (std::exception &e) {
        std::cout << "nlopt failed: " << e.what() << std::endl;
    }

    parameters_->print();

    auto charges = Charges();
    for (const auto &molecule: set_.molecules()) {
        charges.insert(molecule.name(), method_->calculate_charges(molecule));
    }
    charges.save_to_file(charge_output_file_);
}


Parameterization::Parameterization(const MoleculeSet &ms, boost::shared_ptr<Method> method,
                                   const Charges &reference_charges, const std::string &charge_output_file) :
        set_{ms}, method_{method}, reference_charges_{reference_charges},
        charge_output_file_{charge_output_file} {

    parameters_ = std::unique_ptr<Parameters>();
    parameters_ = std::make_unique<Parameters>(ms, method);

    method_->set_parameters(parameters_.get());
}