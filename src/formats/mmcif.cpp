//
// Created by krab1k on 28.1.19.
//

#include <string>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <fmt/format.h>
#include <boost/algorithm/string.hpp>
#include <gemmi/cif.hpp>
#include <gemmi/mmcif.hpp>

#include "chargefw2.h"
#include "../config.h"
#include "mmcif.h"
#include "common.h"
#include "bonds.h"
#include "../periodic_table.h"


void read_protein_molecule(gemmi::cif::Block &data, std::unique_ptr<std::vector<Atom>> &atoms);

void read_ccd_molecule(gemmi::cif::Block &data, std::unique_ptr<std::vector<Atom>> &atoms);


void read_protein_molecule(gemmi::cif::Block &data, std::unique_ptr<std::vector<Atom>> &atoms) {
    auto structure = gemmi::make_structure_from_block(data);
    if (structure.models.empty()) {
        fmt::print(stderr, "Not enough information to create a structure from {}\n", data.name);
        exit(EXIT_FILE_ERROR);
    }

    /* Read first model only */
    auto model = structure.models[0];
    size_t idx = 0;
    for (const auto &chain: model.chains) {
        for (const auto &residue: chain.residues) {
            bool hetatm = residue.het_flag == 'H';
            for (const auto &atom: residue.atoms) {
                double x = atom.pos.x;
                double y = atom.pos.y;
                double z = atom.pos.z;
                int residue_id = hetatm ? residue.seqid.num.value : residue.label_seq.value;
                auto element = PeriodicTable::pte().get_element_by_symbol(get_element_symbol(atom.element.name()));

                if (not atom.has_altloc() or not is_already_loaded(*atoms, atom.name, residue_id)) {
                    if ((not hetatm) or
                        (config::read_hetatm and residue.name != "HOH") or
                        (config::read_hetatm and not config::ignore_water)) {
                        atoms->emplace_back(idx, element, x, y, z, atom.name, residue_id, residue.name, chain.name, hetatm);
                        idx++;
                    }
                }
            }
        }
    }
}


void read_ccd_molecule(gemmi::cif::Block &data, std::unique_ptr<std::vector<Atom>> &atoms) {
    auto table = data.find("_chem_comp_atom.", {"model_Cartn_x", "model_Cartn_y", "model_Cartn_z",
                                                "type_symbol", "atom_id", "comp_id"});
    size_t idx = 0;
    for (const auto row: table) {
        double x, y, z;
        try {
            x = std::stod(row[0]);
            y = std::stod(row[0]);
            z = std::stod(row[0]);
        } catch (std::exception &) {
            fmt::print(stderr, "Cannot load coordinates for {}\n", data.name);
            exit(EXIT_FILE_ERROR);
        }

        auto element = PeriodicTable::pte().get_element_by_symbol(get_element_symbol(row[3]));
        auto atom_name = row[4];
        auto residue = row[5];
        auto residue_id = 0;

        atoms->emplace_back(idx, element, x, y, z, atom_name, residue_id, residue, "0", false);
        idx++;
    }
}


MoleculeSet mmCIF::read_file(const std::string &filename) {

    gemmi::cif::Document doc;
    try {
         doc = gemmi::cif::read_file(filename);
    }
    catch (std::exception &){
        fmt::print(stderr, "Cannot open file: {}\n", filename);
        exit(EXIT_FILE_ERROR);
    }

    auto molecules = std::make_unique<std::vector<Molecule>>();
    for (auto &data: doc.blocks) {
        const auto names = data.get_mmcif_category_names();

        auto atoms = std::make_unique<std::vector<Atom>>();
        if (std::find(names.begin(), names.end(), "_atom_site.") != names.end()) {
            read_protein_molecule(data, atoms);
        } else if (std::find(names.begin(), names.end(), "_chem_comp_atom.") != names.end()) {
            read_ccd_molecule(data, atoms);
        }

        if (atoms->empty()) {
            fmt::print(stderr, "No atoms were loaded from the input file.\n");
            exit(EXIT_FILE_ERROR);
        }

        auto bonds = get_bonds(atoms);
        std::map<size_t, int> charges;
        molecules->emplace_back(data.name, std::move(atoms), std::move(bonds), charges);
    }
    return MoleculeSet(std::move(molecules));
}
