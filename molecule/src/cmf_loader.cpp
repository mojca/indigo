/****************************************************************************
 * Copyright (C) 2009-2011 GGA Software Services LLC
 * 
 * This file is part of Indigo toolkit.
 * 
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 3 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 * 
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ***************************************************************************/

#include "base_cpp/scanner.h"

#include "molecule/molecule.h"
#include "molecule/cmf_loader.h"
#include "molecule/cmf_symbol_codes.h"

using namespace indigo;

CmfLoader::CmfLoader (LzwDict &dict, Scanner &scanner) :
TL_CP_GET(_atoms),
TL_CP_GET(_bonds),
TL_CP_GET(_pseudo_labels),
TL_CP_GET(_attachments),
TL_CP_GET(_sgroup_order)
{
   _init();
   _decoder_obj.create(dict, scanner);
   _lzw_scanner.create(_decoder_obj.ref());
   _scanner = _lzw_scanner.get();
}

CmfLoader::CmfLoader (Scanner &scanner) :
TL_CP_GET(_atoms), TL_CP_GET(_bonds), TL_CP_GET(_pseudo_labels), TL_CP_GET(_attachments), TL_CP_GET(_sgroup_order)
{
   _init();
   _scanner = &scanner;
}

CmfLoader::CmfLoader (LzwDecoder &decoder) :
TL_CP_GET(_atoms), TL_CP_GET(_bonds), TL_CP_GET(_pseudo_labels), TL_CP_GET(_attachments), TL_CP_GET(_sgroup_order)
{
   _init();
   _lzw_scanner.create(decoder);
   _scanner = _lzw_scanner.get();
}

CmfLoader::~CmfLoader()
{
   // Do nothing, but without explicit destructor Visual Studio produces invalid code
   // that leads to "Attempted to read or write protected memory".
}

void CmfLoader::_init ()
{
   skip_cistrans = false;
   skip_stereocenters = false;
   skip_valence = false;
   _ext_decoder = 0;
   _scanner = 0;
   atom_flags = 0;
   bond_flags = 0;

   _sgroup_order.clear();
}

bool CmfLoader::_getNextCode (int &code)
{
   if (_scanner->isEOF())
      return false;
   code = _scanner->readByte();
   return true;
}

bool CmfLoader::_readAtom (int &code, _AtomDesc &atom, int atom_idx)
{
   if (code > 0 && code < ELEM_MAX)
      atom.label = code;
   else if (code == CMF_PSEUDOATOM)
   {
      int len;

      if (!_getNextCode(len))
         throw Error("pseudo-atom identifier must be followed by length");

      if (len < 1)
         throw Error("empty pseudo-atom");

      atom.pseudo_atom_idx = _pseudo_labels.add(len + 1);
      char *label = _pseudo_labels.at(atom.pseudo_atom_idx);

      for (int i = 0; i < len; i++)
      {
         int c;
         if (!_getNextCode(c))
            throw Error("pseudo-atom label is incomplete");
         label[i] = c;
      }

      label[len] = 0;
   }
   else if (code == CMF_RSITE) 
   {
      atom.label = ELEM_RSITE;
      _getNextCode(atom.rsite_bits);
   }
   else if (code == CMF_RSITE_EXT) 
   {
      atom.label = ELEM_RSITE;
      atom.rsite_bits = (int)_scanner->readPackedUInt();
   }
   else
      throw Error("bad atom number: %d", code);

   if (!_getNextCode(code))
      return false;

   if (code >= CMF_CHARGES &&
       code <  CMF_CHARGES + CMF_NUM_OF_CHARGES && code != CMF_SEPARATOR)
   {
      int charge = code - CMF_CHARGES;

      charge += CMF_MIN_CHARGE;

      atom.charge = charge;

      if (!_getNextCode(code))
         return false;
   }

   if (code == CMF_CHARGE_EXT)
   {
      int charge;
      _getNextCode(charge);
      charge -= 128;
      atom.charge = charge;

      if (!_getNextCode(code))
         return false;
   }

   if (code >= CMF_ISOTOPE_ZERO && code <= CMF_ISOTOPE_OTHER)
   {
      int deviation;

      if (code == CMF_ISOTOPE_ZERO)
         deviation = 0;
      else if (code == CMF_ISOTOPE_PLUS1)
         deviation = 1;
      else if (code == CMF_ISOTOPE_PLUS2)
         deviation = 2;
      else if (code == CMF_ISOTOPE_MINUS1)
         deviation = -1;
      else if (code == CMF_ISOTOPE_MINUS2)
         deviation = -2;
      else // CMF_ISOTOPE_OTHER
      {
         if (!_getNextCode(code))
            throw Error("expecting mass difference");

         deviation = code - 100;
      }

      atom.isotope = Element::getDefaultIsotope(atom.label) + deviation;

      if (!_getNextCode(code))
         return false;
   }
   
   if (code >= CMF_RADICAL_SINGLET && code <= CMF_RADICAL_TRIPLET)
   {
      if (code == CMF_RADICAL_SINGLET)
         atom.radical = RADICAL_SINGLET;
      else if (code == CMF_RADICAL_DOUBLET)
         atom.radical = RADICAL_DOUBLET;
      else // code == CMF_RADICAL_TRIPLET
         atom.radical = RADICAL_TRIPLET;
      
      if (!_getNextCode(code))
         return false;
   }
   
   if (code >= CMF_STEREO_ANY && code <= CMF_STEREO_ABS_1)
   {
      if (code >= CMF_STEREO_AND_1)
      {
         /* CMF_STEREO_*_1 -> CMF_STEREO_*_0 */
         code -= CMF_MAX_STEREOGROUPS * 2 + 1;
         atom.stereo_invert_pyramid = true;
      }
      
      if (code == CMF_STEREO_ANY)
         atom.stereo_type = MoleculeStereocenters::ATOM_ANY;
      else if (code == CMF_STEREO_ABS_0)
         atom.stereo_type = MoleculeStereocenters::ATOM_ABS;
      else if (code < CMF_STEREO_OR_0)
      {
         atom.stereo_type = MoleculeStereocenters::ATOM_AND;
         atom.stereo_group = code - CMF_STEREO_AND_0 + 1;
      }
      else
      {
         atom.stereo_type = MoleculeStereocenters::ATOM_OR;
         atom.stereo_group = code - CMF_STEREO_OR_0 + 1;
      }
      if (!_getNextCode(code))
         return false;
   }

   if (code == CMF_STEREO_ALLENE_0 || code == CMF_STEREO_ALLENE_1)
   {
      if (code == CMF_STEREO_ALLENE_0)
         atom.allene_stereo_parity = 1;
      else
         atom.allene_stereo_parity = 2;
      if (!_getNextCode(code))
         return false;
   }

   if (code >= CMF_IMPLICIT_H && code <= CMF_IMPLICIT_H + CMF_MAX_IMPLICIT_H)
   {
      atom.hydrogens = code - CMF_IMPLICIT_H;
      if (!_getNextCode(code))
         return false;
   }

   if (code >= CMF_VALENCE && code <= CMF_VALENCE + CMF_MAX_VALENCE)
   {
      atom.valence = code - CMF_VALENCE;
      if (!_getNextCode(code))
         return false;
   }

   if (code == CMF_VALENCE_EXT)
   {
      atom.valence = (int)_scanner->readPackedUInt();
      if (!_getNextCode(code))
         return false;
   }

   while (code == CMF_ATTACHPT)
   {
      int aidx;

      if (!_getNextCode(aidx))
         throw Error("expected attachment index");

      _AttachmentDesc &att = _attachments.push();
      att.atom = atom_idx;
      att.index = aidx;
      
      if (!_getNextCode(code))
         return false;
   }

   while (code >= CMF_ATOM_FLAGS && code < CMF_ATOM_FLAGS + CMF_NUM_OF_ATOM_FLAGS)
   {
      atom.flags |= (1 << (code - CMF_ATOM_FLAGS));
      if (!_getNextCode(code))
         return false;
   }

   if (code == CMF_HIGHLIGHTED)
   {
      atom.highlighted = true;
      if (!_getNextCode(code))
         return false;
   }
   
   return true;
}

void CmfLoader::_readBond (int &code, _BondDesc &bond)
{
   bond.cis_trans = 0;
   bond.flags = 0;
   bond.swap = false;
   bond.direction = 0;
   bond.highlighted = false;
   
   if (code == CMF_BOND_SINGLE_CHAIN)
   {
      bond.type = BOND_SINGLE;
      bond.in_ring = false;
   }
   else if (code == CMF_BOND_SINGLE_RING)
   {
      bond.type = BOND_SINGLE;
      bond.in_ring = true;
   }
   else if (code == CMF_BOND_DOUBLE_CHAIN)
   {
      bond.type = BOND_DOUBLE;
      bond.in_ring = false;
   }
   else if (code == CMF_BOND_DOUBLE_RING)
   {
      bond.type = BOND_DOUBLE;
      bond.in_ring = true;
   }
   else if (code == CMF_BOND_DOUBLE_CHAIN_CIS)
   {
      bond.type = BOND_DOUBLE;
      bond.in_ring = false;
      bond.cis_trans = MoleculeCisTrans::CIS;
   }
   else if (code == CMF_BOND_DOUBLE_CHAIN_TRANS)
   {
      bond.type = BOND_DOUBLE;
      bond.in_ring = false;
      bond.cis_trans = MoleculeCisTrans::TRANS;
   }
   else if (code == CMF_BOND_DOUBLE_RING_CIS)
   {
      bond.type = BOND_DOUBLE;
      bond.in_ring = true;
      bond.cis_trans = MoleculeCisTrans::CIS;
   }
   else if (code == CMF_BOND_DOUBLE_RING_TRANS)
   {
      bond.type = BOND_DOUBLE;
      bond.in_ring = true;
      bond.cis_trans = MoleculeCisTrans::TRANS;
   }
   else if (code == CMF_BOND_TRIPLE_CHAIN)
   {
      bond.type = BOND_TRIPLE;
      bond.in_ring = false;
   }
   else if (code == CMF_BOND_TRIPLE_RING)
   {
      bond.type = BOND_TRIPLE;
      bond.in_ring = true;
   }
   else if (code == CMF_BOND_AROMATIC)
   {
      bond.type = BOND_AROMATIC;
      bond.in_ring = true;
   }
   else if (code == CMF_BOND_DOUBLE_IGNORED_CIS_TRANS_CHAIN || code == CMF_BOND_DOUBLE_IGNORED_CIS_TRANS_RING)
   {
      bond.cis_trans = -1;
      bond.type = BOND_DOUBLE;
      bond.in_ring = (code == CMF_BOND_DOUBLE_IGNORED_CIS_TRANS_RING);
   }
   else
      throw Error("cannot decode bond: code %d", code);

   while (true)
   {
      if (!_getNextCode(code))
         throw Error("nothing is after the bond code");
   
      if (code >= CMF_BOND_FLAGS && code < CMF_BOND_FLAGS + CMF_NUM_OF_BOND_FLAGS)
         bond.flags |= (1 << (code - CMF_BOND_FLAGS));
      else if (code == CMF_BOND_UP)
         bond.direction = BOND_UP;
      else if (code == CMF_BOND_DOWN)
         bond.direction = BOND_DOWN;
      else if (code == CMF_BOND_EITHER)
         bond.direction = BOND_EITHER;
      else if (code == CMF_BOND_SWAP_ENDS)
         bond.swap = true;
      else if (code == CMF_HIGHLIGHTED)
         bond.highlighted = true;
      else
         break;
   }
}

bool CmfLoader::_readCycleNumber (int &code, int &n)
{
   n = 0;

   while (code == CMF_CYCLES_PLUS)
   {
      n += CMF_NUM_OF_CYCLES;
      if (!_getNextCode(code))
         throw Error("CYCLES_PLUS symbol must not be the last one");
   }

   if (code >= CMF_CYCLES && code < CMF_CYCLES + CMF_NUM_OF_CYCLES)
   {
      n += code - CMF_CYCLES;
      return true;
   }
   else if (n > 0)
      throw Error("CYCLES_PLUS symbol must be followed by a cycle number");

   return false;
}

void CmfLoader::loadMolecule (Molecule &mol)
{
   int code;

   mol.clear();

   QS_DEF(Array<int>, cycle_numbers);
   QS_DEF(Array<int>, atom_stack);

   _atoms.clear();
   _bonds.clear();
   _pseudo_labels.clear();
   _attachments.clear();
   cycle_numbers.clear();
   atom_stack.clear();

   bool first_atom = true;

   if (!_getNextCode(code))
      return;

   bool has_ext_part = false;

   /* Main loop */
   do 
   {
      _BondDesc *bond = 0;

      if (code > CMF_ALPHABET_SIZE)
         throw Error("unexpected code");

      if (code == CMF_TERMINATOR)
         break;

      if (code == CMF_EXT)
      {
         has_ext_part = true;
         // Ext part has to be read till CMF_TERMINATOR
         break;
      }

      if (!first_atom)
      {
         int number;

         while (_readCycleNumber(code, number))
         {
            while (cycle_numbers.size() <= number)
               cycle_numbers.push(-1);

            if (cycle_numbers[number] >= 0)
               throw Error("cycle #%d already in use", number);
            
            cycle_numbers[number] = atom_stack.top();

            if (!_getNextCode(code))
               break;
         }
      }

      if (code == CMF_SEPARATOR)
      {
         atom_stack.pop();
         first_atom = true;

         if (!_getNextCode(code))
            break;

         continue;
      }

      if (code == CMF_OPEN_BRACKET)
      {
         atom_stack.push(atom_stack.top());

         if (!_getNextCode(code))
            break;

         continue;
      }

      if (code == CMF_CLOSE_BRACKET)
      {
         atom_stack.pop();

         if (!_getNextCode(code))
            break;

         continue;
      }

      if (!first_atom)
      {
         bond = &_bonds.push();
         bond->beg = atom_stack.top();
      }

      if (bond != 0)
      {
         _readBond(code, *bond);

         int number;

         if (_readCycleNumber(code, number))
         {
            if (cycle_numbers[number] < 0)
               throw Error("bad cycle number after bond symbol");

            bond->end = cycle_numbers[number];
            cycle_numbers[number] = -1;

            if (!_getNextCode(code))
               break;

            continue;
         }
      }

      _AtomDesc &atom = _atoms.push();

      if (!first_atom)
         atom_stack.pop();

      atom_stack.push(_atoms.size() - 1);

      first_atom = false;

      if (bond != 0)
         bond->end = _atoms.size() - 1;

      memset(&atom, 0, sizeof(_AtomDesc));
      atom.hydrogens = -1;
      atom.valence = -1;
      atom.pseudo_atom_idx = -1;
      atom.rsite = false;

      if (code > 0 && (code < ELEM_MAX || code == CMF_PSEUDOATOM || code == CMF_RSITE || code == CMF_RSITE_EXT))
      {
         if (!_readAtom(code, atom, _atoms.size() - 1))
            break;
         continue;
      }

      if (!_getNextCode(code))
         break;

   } while (true); 

   // if have internal decoder, finish it
/*   if (_decoder_obj.get() != 0)
      _decoder_obj->finish(); */

   /* Reading finished, filling molecule */

   int i;

   for (i = 0; i < _atoms.size(); i++)
   {
      mol.addAtom(_atoms[i].label);

      if (_atoms[i].pseudo_atom_idx >= 0)
         mol.setPseudoAtom(i, _pseudo_labels.at(_atoms[i].pseudo_atom_idx));

      if (_atoms[i].rsite_bits > 0)
         mol.setRSiteBits(i, _atoms[i].rsite_bits);

      mol.setAtomCharge(i, _atoms[i].charge);
      mol.setAtomIsotope(i, _atoms[i].isotope);
      if (_atoms[i].hydrogens >= 0)
         mol.setImplicitH(i, _atoms[i].hydrogens);
      mol.setAtomRadical(i, _atoms[i].radical);

      if (_atoms[i].highlighted)
         mol.highlightAtom(i);
   }

   for (i = 0; i < _bonds.size(); i++)
   {
      int type = _bonds[i].type;
      int beg = _bonds[i].beg;
      int end = _bonds[i].end;
      int tmp;

      if (_bonds[i].swap)
         __swap(beg, end, tmp);

      int idx = mol.addBond_Silent(beg, end, type);
      
      if (_bonds[i].in_ring)
         mol.setEdgeTopology(idx, TOPOLOGY_RING);
      else
         mol.setEdgeTopology(idx, TOPOLOGY_CHAIN);

      if (_bonds[i].direction != 0)
         mol.setBondDirection(idx, _bonds[i].direction);

      if (_bonds[i].highlighted)
         mol.highlightBond(idx);
   }

   for (i = 0; i < _attachments.size(); i++)
      mol.addAttachmentPoint(_attachments[i].index, _attachments[i].atom);

   mol.validateEdgeTopologies();

   if (has_ext_part)
      _readExtSection(mol);

   if (atom_flags != 0)
   {
      atom_flags->clear();

      for (i = 0; i < _atoms.size(); i++)
         atom_flags->push(_atoms[i].flags);
   }

   if (bond_flags != 0)
   {
      bond_flags->clear();

      for (i = 0; i < _bonds.size(); i++)
         bond_flags->push(_bonds[i].flags);
   }

   if (!skip_cistrans)
   {
      for (i = 0; i < _bonds.size(); i++)
      {
         if (_bonds[i].cis_trans != 0)
         {
            int parity = _bonds[i].cis_trans;
            if (parity > 0)
               mol.cis_trans.setParity(i, _bonds[i].cis_trans);
            else
               mol.cis_trans.ignore(i);
            mol.cis_trans.restoreSubstituents(i);
         }
      }
   }

   if (!skip_valence)
   {
      for (i = 0; i < _atoms.size(); i++)
      {
         if (_atoms[i].valence >= 0)
            mol.setValence(i, _atoms[i].valence);
      }
   }

   if (!skip_stereocenters)
   {
      for (i = 0; i < _atoms.size(); i++)
      {
         if (_atoms[i].stereo_type != 0)
            mol.stereocenters.add(i, _atoms[i].stereo_type, _atoms[i].stereo_group, _atoms[i].stereo_invert_pyramid);
      }
   }

   for (i = 0; i < _atoms.size(); i++)
   {
      if (_atoms[i].allene_stereo_parity != 0)
      {
         int left, right, subst[4];
         bool pure_h[4];
         int parity = _atoms[i].allene_stereo_parity;
         int tmp;

         if (!MoleculeAlleneStereo::possibleCenter(mol, i, left, right, subst, pure_h))
            throw Error("invalid molecule allene stereo marker");

         if (subst[1] != -1 && subst[1] < subst[0])
            __swap(subst[1], subst[0], tmp);

         if (subst[3] != -1 && subst[3] < subst[2])
            __swap(subst[3], subst[2], tmp);

         if (pure_h[0])
         {
            __swap(subst[1], subst[0], tmp);
            parity = 3 - parity;
         }
         if (pure_h[2])
         {
            __swap(subst[2], subst[3], tmp);
            parity = 3 - parity;
         }

         mol.allene_stereo.add(i, left, right, subst, parity);
      }
   }

   // for loadXyz()
   _mol = &mol;
}

void CmfLoader::_readSGroup (int code, Molecule &mol)
{
   if (code == CMF_DATASGROUP)
   {
      int idx = mol.data_sgroups.add();
      BaseMolecule::DataSGroup &s = mol.data_sgroups[idx];
      _readGeneralSGroup(s);

      _readString(s.description);
      _readString(s.data);
      byte bits = _scanner->readByte();
      s.dasp_pos = bits & 0x0F;
      s.detached = (bits & (1 << 4)) != 0;
      s.relative = (bits & (1 << 5)) != 0;
      s.display_units = (bits & (1 << 6)) != 0;
   }
   else if (code == CMF_SUPERATOM)
   {
      int idx = mol.superatoms.add();
      BaseMolecule::Superatom &s = mol.superatoms[idx];
      _readGeneralSGroup(s);
      _readString(s.subscript);
      s.bond_idx = (int)_scanner->readPackedUInt() - 1;
   }
   else if (code == CMF_REPEATINGUNIT)
   {
      int idx = mol.repeating_units.add();
      BaseMolecule::RepeatingUnit &s = mol.repeating_units[idx];
      _readGeneralSGroup(s);
      _readString(s.subscript);
      s.connectivity = _scanner->readPackedUInt();
   }
   else if (code == CMF_MULTIPLEGROUP)
   {
      int idx = mol.multiple_groups.add();
      BaseMolecule::MultipleGroup &s = mol.multiple_groups[idx];
      _readGeneralSGroup(s);
      _readUIntArray(s.parent_atoms);
      s.multiplier = _scanner->readPackedUInt();
   }
   else if (code == CMF_GENERICSGROUP)
   {
      int idx = mol.generic_sgroups.add();
      _readGeneralSGroup(mol.generic_sgroups[idx]);
   }
   else
      throw Error("_readExtSection: unexpected SGroup code: %d", code);

   _sgroup_order.push(code);
}

float CmfLoader::_readFloatInRange (Scanner &scanner, float min, float range)
{
   return min + ((float)scanner.readBinaryWord() / 65535) * range;
}

void CmfLoader::_readVec3f (Scanner &scanner, Vec3f &pos, const CmfSaver::VecRange &range)
{
   pos.x = _readFloatInRange(scanner, range.xyz_min.x, range.xyz_range.x);
   pos.y = _readFloatInRange(scanner, range.xyz_min.y, range.xyz_range.y);

   if (range.have_z)
      pos.z = _readFloatInRange(scanner, range.xyz_min.z, range.xyz_range.z);
   else
      pos.z = 0;
}

void CmfLoader::_readVec2f (Scanner &scanner, Vec2f &pos, const CmfSaver::VecRange &range)
{
   pos.x = _readFloatInRange(scanner, range.xyz_min.x, range.xyz_range.x);
   pos.y = _readFloatInRange(scanner, range.xyz_min.y, range.xyz_range.y);
}

void CmfLoader::_readDir2f (Scanner &scanner, Vec2f &dir, const CmfSaver::VecRange &range)
{
   dir.x = _readFloatInRange(scanner, range.xyz_min.x, 2 * range.xyz_range.x);
   dir.y = _readFloatInRange(scanner, range.xyz_min.y, 2 * range.xyz_range.y);
}


void CmfLoader::_readBaseSGroupXyz (Scanner &scanner, BaseMolecule::SGroup &sgroup, const CmfSaver::VecRange &range)
{
   int len = scanner.readPackedUInt();
   sgroup.brackets.resize(len);
   for (int i = 0; i < len; i++)
   {
      _readVec2f(scanner, sgroup.brackets[i][0], range);
      _readVec2f(scanner, sgroup.brackets[i][1], range);
   }
}

void CmfLoader::_readSGroupXYZ (Scanner &scanner, int code, int idx_array[5], Molecule &mol, const CmfSaver::VecRange &range)
{
   if (code == CMF_DATASGROUP)
   {
      int idx = idx_array[0]++;
      BaseMolecule::DataSGroup &s = mol.data_sgroups[idx];
      _readBaseSGroupXyz(scanner, s, range);
      _readVec2f(scanner, s.display_pos, range);
   }
   else if (code == CMF_SUPERATOM)
   {
      int idx = idx_array[1]++;
      BaseMolecule::Superatom &s = mol.superatoms[idx];
      _readBaseSGroupXyz(scanner, s, range);
      if (s.bond_idx != -1)
         _readDir2f(scanner, s.bond_dir, range);
   }
   else if (code == CMF_REPEATINGUNIT)
   {
      int idx = idx_array[2]++;
      _readBaseSGroupXyz(scanner, mol.repeating_units[idx], range);
   }
   else if (code == CMF_MULTIPLEGROUP)
   {
      int idx = idx_array[3]++;
      _readBaseSGroupXyz(scanner, mol.multiple_groups[idx], range);
   }
   else if (code == CMF_GENERICSGROUP)
   {
      int idx = idx_array[4]++;
      _readBaseSGroupXyz(scanner, mol.generic_sgroups[idx], range);
   }
   else
      throw Error("_readExtSection: unexpected SGroup code: %d", code);
}


void CmfLoader::_readString (Array<char> &dest)
{
   unsigned int len = _scanner->readPackedUInt();
   dest.resize(len + 1);
   _scanner->read(len, dest.ptr());
   dest[len] = 0;
}

void CmfLoader::_readUIntArray (Array<int> &dest)
{
   unsigned int len = _scanner->readPackedUInt();
   dest.clear_resize(len);
   for (unsigned int i = 0; i < len; i++)
      dest[i] = _scanner->readPackedUInt();
}
 
void CmfLoader::_readGeneralSGroup (BaseMolecule::SGroup &sgroup)
{
   _readUIntArray(sgroup.atoms);
   _readUIntArray(sgroup.bonds);
}
 
void CmfLoader::_readExtSection (Molecule &mol)
{
   _sgroup_order.clear();
   int code;
   while (true)
   {
      if (!_getNextCode(code))
         throw Error("_readExtSection: unexpected end of the stream");

      if (code == CMF_TERMINATOR)
         break;
      // TODO provide a readers map to avoid such "if"s
      else if (code == CMF_DATASGROUP || code == CMF_SUPERATOM || 
         code == CMF_REPEATINGUNIT || code == CMF_MULTIPLEGROUP || code == CMF_GENERICSGROUP)
      {
         _readSGroup(code, mol);   
      }
      else if (code == CMF_RSITE_ATTACHMENTS)
      {
         int idx = _scanner->readPackedUInt();
         int count = _scanner->readPackedUInt();
         for (int i = 0; i < count; i++)
         {
            int idx2 = _scanner->readPackedUInt();
            mol.setRSiteAttachmentOrder(idx, idx2, i);
         }
      }
      else
         throw Error("unexpected code: %d", code);
   }
}

void CmfLoader::loadXyz (Scanner &scanner)
{
   if (_mol == 0)
      throw Error("loadMolecule() must be called prior to loadXyz()");

   int i;

   CmfSaver::VecRange range;

   range.xyz_min.x = scanner.readBinaryFloat();
   range.xyz_min.y = scanner.readBinaryFloat();
   range.xyz_min.z = scanner.readBinaryFloat();

   range.xyz_range.x = scanner.readBinaryFloat();
   range.xyz_range.y = scanner.readBinaryFloat();
   range.xyz_range.z = scanner.readBinaryFloat();

   range.have_z = (scanner.readByte() != 0);

   for (i = 0; i < _atoms.size(); i++)
   {
      Vec3f pos;
      _readVec3f(scanner, pos, range);

      _mol->setAtomXyz(i, pos.x, pos.y, pos.z);
   }

   // Read sgroup coordinates data
   int idx[5] = {0};
   for (int i = 0; i < _sgroup_order.size(); i++)
      _readSGroupXYZ(scanner, _sgroup_order[i], idx, *_mol, range);

   _mol->have_xyz = true;
}
