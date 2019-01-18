//
// Created by krab1k on 31/10/18.
//

#include <vector>
#include <cmath>
#include <mkl_lapacke.h>
#include <mkl.h>

#include "eqeqc.h"
#include "../parameters.h"
#include "../geometry.h"


#define IDX(i, j) ((i) * m + (j))

std::vector<double> EQeqC::calculate_charges(const Molecule &molecule) const {

    size_t n = molecule.atoms().size();
    size_t m = n + 1;

    const double lambda = 1.2;
    const double k = 14.4;
    double H_electron_affinity = -2.0; // Exception for hydrogen mentioned in the article

    auto *A = static_cast<double *>(mkl_malloc(m * m * sizeof(double), 64));
    auto *b = static_cast<double *>(mkl_malloc(m * sizeof(double), 64));
    auto *ipiv = static_cast<int *>(mkl_malloc(m * sizeof(int), 64));

    std::vector<double> X(n, 0);
    std::vector<double> J(n, 0);

    for (size_t i = 0; i < n; i++) {
        const auto &atom_i = molecule.atoms()[i];
        if (atom_i.element().symbol() == "H") {
            X[i] = (atom_i.element().ionization_potential() + H_electron_affinity) / 2;
            J[i] = atom_i.element().ionization_potential() - H_electron_affinity;
        } else {
            X[i] = (atom_i.element().ionization_potential() + atom_i.element().electron_affinity()) / 2;
            J[i] = atom_i.element().ionization_potential() - atom_i.element().electron_affinity();
        }
    }

    for (size_t i = 0; i < n; i++) {
        const auto &atom_i = molecule.atoms()[i];
        A[IDX(i, i)] = J[i];
        b[i] = -X[i];
        for (size_t j = i + 1; j < n; j++) {
            const auto &atom_j = molecule.atoms()[j];
            double a = std::sqrt(J[i] * J[j]) / k;
            double Rij = distance(atom_i, atom_j);
            double overlap = std::exp(-a * a * Rij * Rij) * (2 * a - a * a * Rij - 1 / Rij);
            A[IDX(i, j)] = lambda * k / 2 * (1 / Rij + overlap);
        }
    }

    for (size_t i = 0; i < n; i++) {
        A[IDX(i, n)] = 1;
    }

    A[IDX(n, n)] = 0;
    b[n] = molecule.total_charge();

    int info = LAPACKE_dsysv(LAPACK_ROW_MAJOR, 'U', m, 1, A, m, ipiv, b, 1);
    if (info) {
        throw std::runtime_error("Cannot solve linear system");
    }

    // Above is the same as EQeq, now add bond-order-corrections
    for (size_t i = 0; i < n; i++) {
        const auto &atom_i = molecule.atoms()[i];
        double correction = 0;
        for (size_t j = 0; j < n; j++) {
            if (i == j)
                continue;
            const auto &atom_j = molecule.atoms()[j];
            double tkk = parameters_->atom()->parameter(atom::Dz)(atom_i) - parameters_->atom()->parameter(atom::Dz)(atom_j);
            double bkk = std::exp(-parameters_->common()->parameter(common::alpha) *
                                  (distance(atom_i, atom_j) - atom_i.element().covalent_radius() +
                                   atom_j.element().covalent_radius()));
            correction += tkk * bkk;
        }
        b[i] += correction;
    }

    std::vector<double> results;
    results.assign(b, b + n);

    mkl_free(A);
    mkl_free(b);
    mkl_free(ipiv);

    return results;
}