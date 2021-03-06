/****************************************************************************
 * Copyright (C) 2011 GGA Software Services LLC
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

#include "indigo_mapping.h"
#include "indigo_molecule.h"
#include "indigo_reaction.h"

IndigoMapping::IndigoMapping (BaseMolecule &from_, BaseMolecule &to_) :
IndigoObject(MAPPING),
from(from_),
to(to_)
{
}

IndigoMapping::~IndigoMapping ()
{
}

IndigoObject * IndigoMapping::clone ()
{
   AutoPtr<IndigoMapping> res_ptr(new IndigoMapping(from, to));
   res_ptr->mapping.copy(mapping);
   return res_ptr.release();
}

IndigoReactionMapping::IndigoReactionMapping (BaseReaction &from_, BaseReaction &to_) :
IndigoObject(REACTION_MAPPING),
from(from_),
to(to_)
{
}

IndigoReactionMapping::~IndigoReactionMapping ()
{
}

IndigoObject * IndigoReactionMapping::clone ()
{
   AutoPtr<IndigoReactionMapping> res_ptr(new IndigoReactionMapping(from, to));
   res_ptr->mol_mapping.copy(mol_mapping);
   for (int i = 0; i < mappings.size(); ++i) {
      res_ptr->mappings.push().copy(mappings[i]);
   }
   return res_ptr.release();
}

CEXPORT int indigoMapAtom (int handle, int atom)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(handle);
      IndigoAtom &ia = IndigoAtom::cast(self.getObject(atom));

      if (obj.type == IndigoObject::MAPPING)
      {
         IndigoMapping &mapping = (IndigoMapping &)obj;

         int mapped = mapping.mapping[ia.idx];
         if (mapped < 0)
            return 0;
         return self.addObject(new IndigoAtom(mapping.to, mapped));
      }
      if (obj.type == IndigoObject::REACTION_MAPPING)
      {
         IndigoReactionMapping &mapping = (IndigoReactionMapping &)obj;

         int mol_idx = mapping.from.findMolecule(&ia.mol);

         if (mol_idx == -1)
            throw IndigoError("indigoMapAtom(): input atom not found in the reaction");

         if (mapping.mol_mapping[mol_idx] < 0)
            // can happen for catalysts
            return 0;

         BaseMolecule &mol = mapping.to.getBaseMolecule(mapping.mol_mapping[mol_idx]);
         int idx = mapping.mappings[mol_idx][ia.idx];

         if (idx < 0)
            return 0;

         return self.addObject(new IndigoAtom(mol, idx));
      }

      throw IndigoError("indigoMapAtom(): not applicable to %s", obj.debugInfo());
   }
   INDIGO_END(-1)
}

CEXPORT int indigoMapBond (int handle, int bond)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(handle);
      IndigoBond &ib = IndigoBond::cast(self.getObject(bond));

      if (obj.type == IndigoObject::MAPPING)
      {
         IndigoMapping &mapping = (IndigoMapping &)obj;

         const Edge &edge = ib.mol.getEdge(ib.idx);

         int beg = mapping.mapping[edge.beg];
         int end = mapping.mapping[edge.end];

         if (beg < 0 || end < 0)
            return 0;

         int idx = mapping.to.findEdgeIndex(beg, end);

         if (idx < 0)
            return 0;

         return self.addObject(new IndigoBond(mapping.to, idx));
      }
      if (obj.type == IndigoObject::REACTION_MAPPING)
      {
         IndigoReactionMapping &mapping = (IndigoReactionMapping &)obj;

         int mol_idx = mapping.from.findMolecule(&ib.mol);

         if (mol_idx == -1)
            throw IndigoError("indigoMapBond(): input bond not found in the reaction");

         if (mapping.mol_mapping[mol_idx] < 0)
            return 0; // can happen with catalysts

         BaseMolecule &mol = mapping.to.getBaseMolecule(mapping.mol_mapping[mol_idx]);
         const Edge &edge = ib.mol.getEdge(ib.idx);

         int beg = mapping.mappings[mol_idx][edge.beg];
         int end = mapping.mappings[mol_idx][edge.end];

         if (beg < 0 || end < 0)
            return 0;

         int idx = mol.findEdgeIndex(beg, end);

         if (idx < 0)
            return 0;

         return self.addObject(new IndigoBond(mol, idx));
      }

      throw IndigoError("indigoMapBond(): not applicable to %s", obj.debugInfo());
   }
   INDIGO_END(-1)
}

void _indigoHighlightSubstructure (BaseMolecule &query, BaseMolecule &mol, Array<int> &qmapping, Array<int> &mapping)
{
   int i;

   for (i = query.vertexBegin(); i != query.vertexEnd(); i = query.vertexNext(i))
   {
      int mapped = qmapping[i];
      if (mapped >= 0 && mapping[mapped] >= 0)
         mol.highlightAtom(mapping[mapped]);
   }
   for (i = query.edgeBegin(); i != query.edgeEnd(); i = query.edgeNext(i))
   {
      const Edge &edge = query.getEdge(i);
      int beg = qmapping[edge.beg];
      int end = qmapping[edge.end];

      if (beg < 0 || end < 0)
         continue;

      int idx = mol.findEdgeIndex(mapping[beg], mapping[end]);
      if (idx >= 0)
         mol.highlightBond(idx);
   }
}

CEXPORT int indigoHighlightedTarget (int item)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(item);

      if (obj.type == IndigoObject::MAPPING)
      {
         IndigoMapping &im = (IndigoMapping &)obj;
         AutoPtr<IndigoMolecule> mol(new IndigoMolecule());

         QS_DEF(Array<int>, mapping);
         mol->mol.clone(im.to, &mapping, 0);
         _indigoHighlightSubstructure(im.from, mol->mol, im.mapping, mapping);
         return self.addObject(mol.release());
      }
      if (obj.type == IndigoObject::REACTION_MAPPING)
      {
         IndigoReactionMapping &im = (IndigoReactionMapping &)obj;
         AutoPtr<IndigoReaction> rxn(new IndigoReaction());
         QS_DEF(ObjArray< Array<int> >, mappings);
         QS_DEF(Array<int>, mol_mapping);
         int i;

         rxn->rxn.clone(im.to, &mol_mapping, &mappings, 0);

         for (i = im.from.begin(); i != im.from.end(); i = im.from.next(i))
         {
            if (im.mol_mapping[i] < 0)
               // can happen with catalysts
               continue;
            _indigoHighlightSubstructure(
                    im.from.getBaseMolecule(i),
                    rxn->rxn.getBaseMolecule(mol_mapping[im.mol_mapping[i]]),
                    im.mappings[i], mappings[im.mol_mapping[i]]);
         }
         return self.addObject(rxn.release());
      }
      
      throw IndigoError("indigoHighlightedTarget(): no idea what to do with %s", obj.debugInfo());
   }
   INDIGO_END(-1)
}
