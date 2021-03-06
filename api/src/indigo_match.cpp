/****************************************************************************
 * Copyright (C) 2010-2011 GGA Software Services LLC
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

#include "indigo_match.h"
#include "indigo_molecule.h"
#include "indigo_reaction.h"
#include "reaction/reaction_substructure_matcher.h"
#include "molecule/molecule_exact_matcher.h"
#include "reaction/reaction_exact_matcher.h"
#include "molecule/molecule_tautomer_matcher.h"
#include "base_cpp/scanner.h"
#include "indigo_mapping.h"
#include "molecule/elements.h"
#include "reaction/reaction_automapper.h"

void _indigoParseTauCondition (const char *list_ptr, int &aromaticity, Array<int> &label_list)
{
   if (list_ptr == 0 || *list_ptr == 0)
      throw IndigoError("null or empty tautomer rule description is not allowed");

   if (isdigit(*list_ptr))
   {
      if (*list_ptr == '1')
         aromaticity = 1;
      else if (*list_ptr == '0')
         aromaticity = 0;
      else
         throw IndigoError("bad tautomer rule format");
      list_ptr++;
   } else {
      aromaticity = -1;
   }

   label_list.clear();

   QS_DEF(Array<char>, buf);
   buf.clear();

   while (*list_ptr != 0)
   {
      if (isalpha((unsigned)*list_ptr))
         buf.push(*list_ptr);
      else if (*list_ptr == ',')
      {
         buf.push(0);
         label_list.push(Element::fromString(buf.ptr()));
         buf.clear();
      } else
         throw IndigoError("bad label list format in the tautomer rule");
      list_ptr++;
   }

   buf.push(0);
   label_list.push(Element::fromString(buf.ptr()));
}

CEXPORT int indigoSetTautomerRule (int n, const char *beg, const char *end)
{
   INDIGO_BEGIN
   {
      if (n < 1 || n >= 32)
         throw IndigoError("tautomer rule index %d is out of range", n);

      self.tautomer_rules.expand(n);
      
      if (self.tautomer_rules[n - 1] != 0)
      {
         delete self.tautomer_rules[n - 1];
         self.tautomer_rules[n - 1] = 0;
      }

      AutoPtr<TautomerRule> rule(new TautomerRule());

      _indigoParseTauCondition(beg, rule->aromaticity1, rule->list1);
      _indigoParseTauCondition(end, rule->aromaticity2, rule->list2);

      self.tautomer_rules.set(n - 1, rule.release());
      return 1;
   }
   INDIGO_END(-1)
}

CEXPORT int indigoClearTautomerRules ()
{
   INDIGO_BEGIN
   {
      self.tautomer_rules.clear();
      return 1;
   }
   INDIGO_END(-1)
}

CEXPORT int indigoRemoveTautomerRule (int n)
{
   INDIGO_BEGIN
   {
      self.tautomer_rules.remove(n - 1);
      return 1;
   }
   INDIGO_END(-1)
}

static bool _indigoParseTautomerFlags (const char *flags, IndigoTautomerParams &params)
{
   if (flags == 0)
      return false;

   BufferScanner scanner(flags);

   scanner.skipSpace();

   QS_DEF(Array<char>, word);

   if (scanner.isEOF())
      return false;

   scanner.readWord(word, 0);

   if (strcasecmp(word.ptr(), "TAU") != 0)
      return false;

   MoleculeTautomerMatcher::parseConditions(flags, params.conditions, params.force_hydrogens, params.ring_chain);

   return true;
}

int _indigoParseExactFlags (const char *flags, bool reaction, float *rms_threshold)
{
   if (flags == 0)
      throw IndigoError("_indigoParseExactFlags(): zero string pointer");

   if (!reaction && rms_threshold == 0)
      throw IndigoError("_indigoParseExactFlags(): zero float pointer");

   static struct
   {
      const char *token;
      int type; // 1 -- molecule, 2 -- reaction, 3 -- both
      int value;
   } token_list[] =
   {
      {"ELE", 3, MoleculeExactMatcher::CONDITION_ELECTRONS},
      {"MAS", 3, MoleculeExactMatcher::CONDITION_ISOTOPE},
      {"STE", 3, MoleculeExactMatcher::CONDITION_STEREO},
      {"FRA", 1, MoleculeExactMatcher::CONDITION_FRAGMENTS},
      {"AAM", 2, ReactionExactMatcher::CONDITION_AAM},
      {"RCT", 2, ReactionExactMatcher::CONDITION_REACTING_CENTERS}
   };

   int i, res = 0, count = 0;
   bool had_float = false;
   bool had_none = false;
   bool had_all = false;

   if (!reaction)
      *rms_threshold = 0;
   
   BufferScanner scanner(flags);

   QS_DEF(Array<char>, word);
   while (1)
   {
      scanner.skipSpace();
      if (scanner.isEOF())
         break;
      if (had_float)
         throw IndigoError("_indigoParseExactFlags(): no value is allowed after the number");
      scanner.readWord(word, 0);

      if (strcasecmp(word.ptr(), "NONE") == 0)
      {
         if (had_all)
            throw IndigoError("_indigoParseExactFlags(): NONE conflicts with ALL");
         had_none = true;
         count++;
         continue;
      }
      if (strcasecmp(word.ptr(), "ALL") == 0)
      {
         if (had_none)
            throw IndigoError("_indigoParseExactFlags(): ALL conflicts with NONE");
         had_all = true;
         res = MoleculeExactMatcher::CONDITION_ALL;
         if (reaction)
            res |= ReactionExactMatcher::CONDITION_ALL;
         count++;
         continue;
      }
      if (strcasecmp(word.ptr(), "TAU") == 0)
         // should have been handled before
         throw IndigoError("_indigoParseExactFlags(): no flags are allowed together with TAU");

      for (i = 0; i < NELEM(token_list); i++)
      {
         if (strcasecmp(token_list[i].token, word.ptr()) == 0)
         {
            if (!reaction && !(token_list[i].type & 1))
               throw IndigoError("_indigoParseExactFlags(): %s flag is allowed only for reaction matching", word.ptr());
            if (reaction && !(token_list[i].type & 2))
               throw IndigoError("_indigoParseExactFlags(): %s flag is allowed only for molecule matching", word.ptr());
            if (had_all)
               throw IndigoError("_indigoParseExactFlags(): only negative flags are allowed together with ALL");
            res |= token_list[i].value;
            break;
         }
         if (word[0] == '-' && strcasecmp(token_list[i].token, word.ptr() + 1) == 0)
         {
            if (!reaction && !(token_list[i].type & 1))
               throw IndigoError("_indigoParseExactFlags(): %s flag is allowed only for reaction matching", word.ptr());
            if (reaction && !(token_list[i].type & 2))
               throw IndigoError("_indigoParseExactFlags(): %s flag is allowed only for molecule matching", word.ptr());
            res &= ~token_list[i].value;
            break;
         }
      }
      if (i == NELEM(token_list))
      {
         BufferScanner scanner2(word.ptr());

         if (!reaction && scanner2.tryReadFloat(*rms_threshold))
         {
            res |= MoleculeExactMatcher::CONDITION_3D;
            had_float = true;
         }
         else
            throw IndigoError("_indigoParseExactFlags(): unknown token %s", word.ptr());
      }
      else
         count++;
   }

   if (had_none && count > 1)
      throw IndigoError("_indigoParseExactFlags(): no flags are allowed together with NONE");
   
   if (count == 0)
      res = MoleculeExactMatcher::CONDITION_ALL | ReactionExactMatcher::CONDITION_ALL;

   return res;
}

CEXPORT int indigoExactMatch (int handler1, int handler2, const char *flags)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj1 = self.getObject(handler1);
      IndigoObject &obj2 = self.getObject(handler2);

      if (flags == 0)
         flags = "";

      if (IndigoBaseMolecule::is(obj1))
      {
         Molecule &mol1 = obj1.getMolecule();
         Molecule &mol2 = obj2.getMolecule();
         IndigoTautomerParams params;
         AutoPtr<IndigoMapping> mapping(new IndigoMapping(mol1, mol2));

         if (_indigoParseTautomerFlags(flags, params))
         {
            MoleculeTautomerMatcher matcher(mol2, false);

            matcher.setRulesList(&self.tautomer_rules);
            matcher.setRules(params.conditions, params.force_hydrogens, params.ring_chain);
            matcher.setQuery(mol1);

            if (!matcher.find())
               return 0;

            mapping->mapping.copy(matcher.getQueryMapping(), mol1.vertexEnd());
            return self.addObject(mapping.release());
         }

         MoleculeExactMatcher matcher(mol1, mol2);
         matcher.flags = _indigoParseExactFlags(flags, false, &matcher.rms_threshold);

         if (!matcher.find())
            return 0;
         
         mapping->mapping.copy(matcher.getQueryMapping(), mol1.vertexEnd());
         return self.addObject(mapping.release());
      }
      if (IndigoBaseReaction::is(obj1))
      {
         Reaction &rxn1 = obj1.getReaction();
         Reaction &rxn2 = obj2.getReaction();
         int i;

         ReactionExactMatcher matcher(rxn1, rxn2);
         matcher.flags = _indigoParseExactFlags(flags, true, 0);

         if (!matcher.find())
            return 0;

         AutoPtr<IndigoReactionMapping> mapping(new IndigoReactionMapping(rxn1, rxn2));
         mapping->mol_mapping.clear_resize(rxn1.end());
         mapping->mol_mapping.fffill();
         mapping->mappings.expand(rxn1.end());

         for (i = rxn1.begin(); i != rxn1.end(); i = rxn1.next(i))
         {
            if (rxn1.getSideType(i) == BaseReaction::CATALYST)
               continue;
            mapping->mol_mapping[i] = matcher.getTargetMoleculeIndex(i);
            mapping->mappings[i].copy(matcher.getQueryMoleculeMapping(i), rxn1.getBaseMolecule(i).vertexEnd());
         }

         return self.addObject(mapping.release());
      }

      throw IndigoError("indigoExactMatch(): %s is neither a molecule nor a reaction", obj1.debugInfo());
   }
   INDIGO_END(-1);
}

IndigoMoleculeSubstructureMatchIter::IndigoMoleculeSubstructureMatchIter (Molecule &target_,
      QueryMolecule &query_, Molecule &original_target_, bool resonance, bool disable_folding_query_h) :
        IndigoObject(MOLECULE_SUBSTRUCTURE_MATCH_ITER),
        matcher(target_),
        target(target_),
        original_target(original_target_),
        query(query_)
{
   matcher.disable_folding_query_h = disable_folding_query_h;
   matcher.setQuery(query);
   matcher.fmcache = &fmcache;

   matcher.use_pi_systems_matcher = resonance;

   _initialized = false;
   _found = false;
   _need_find = true;
   _embedding_index = 0;
}

IndigoMoleculeSubstructureMatchIter::~IndigoMoleculeSubstructureMatchIter ()
{
}

const char * IndigoMoleculeSubstructureMatchIter::debugInfo ()
{
   return "<molecule substructure match iterator>";
}

IndigoObject * IndigoMoleculeSubstructureMatchIter::next ()
{
   if (!hasNext())
      return 0;

   AutoPtr<IndigoMapping> mptr(new IndigoMapping(query, original_target));

   // Expand mapping to fit possible implicit hydrogens
   mapping.expandFill(target.vertexEnd(), -1);

   if (!matcher.getEmbeddingsStorage().isEmpty())
   {
      const GraphEmbeddingsStorage& storage = matcher.getEmbeddingsStorage();
      int count;
      const int *query_mapping = storage.getMappingSub(_embedding_index, count);
      mptr->mapping.copy(query_mapping, query.vertexEnd());
   }
   else
      mptr->mapping.copy(matcher.getQueryMapping(), query.vertexEnd());

   for (int v = query.vertexBegin(); v != query.vertexEnd(); v = query.vertexNext(v))
   {
      int mapped = mptr->mapping[v];
      
      if (mapped >= 0)
         mptr->mapping[v] = mapping[mapped];
   }
   _need_find = true;
   return mptr.release();
}

bool IndigoMoleculeSubstructureMatchIter::hasNext ()
{
   if (!_need_find)
      return _found;

   if (!_initialized)
   {
      _initialized = true;
      _found = matcher.find();
   }
   else
   {
      _embedding_index++;
      int cur_count = matcher.getEmbeddingsStorage().count();
      if (_embedding_index < cur_count)
         _found = true;
      else
         _found = matcher.findNext();
   }
   if (_embedding_index >= max_embeddings)
      throw IndigoError("Number of embeddings exceeded maximum allowed limit (%d). "
         "Adjust options to raise this limit.", max_embeddings);

   _need_find = false;
   return _found;
}

struct MatchCountContext
{
   int embeddings_count, max_count;
};

static bool _matchCountEmbeddingsCallback (Graph &sub, Graph &super,
                                           const int *core1, const int *core2, void *context_)
{
   MatchCountContext *context = (MatchCountContext *)context_;
   context->embeddings_count++;
   if (context->embeddings_count >= context->max_count)
      return false;
   return true;
}

int IndigoMoleculeSubstructureMatchIter::countMatches (int embeddings_limit)
{
   if (max_embeddings <= 0)
      throw IndigoError("Maximum allowed embeddings limit must be positive "
         "Adjust options to raise this limit.");

   MatchCountContext context;
   context.embeddings_count = 0;
   if (embeddings_limit != 0)
      context.max_count = __min(max_embeddings, embeddings_limit);
   else
      context.max_count = max_embeddings;

   matcher.find_all_embeddings = true;
   matcher.cb_embedding = _matchCountEmbeddingsCallback;
   matcher.cb_embedding_context = &context;
   matcher.find();
   if (embeddings_limit != 0 && context.embeddings_count >= embeddings_limit)
      return embeddings_limit;
   if (context.embeddings_count >= max_embeddings)
      throw IndigoError("Number of embeddings exceeded maximum allowed limit (%d). "
         "Adjust options to raise this limit.", max_embeddings);
   return context.embeddings_count;
}

IndigoMoleculeSubstructureMatcher::IndigoMoleculeSubstructureMatcher (Molecule &target, int mode_) :
   IndigoObject(MOLECULE_SUBSTRUCTURE_MATCHER),
   target(target)
{
   _arom_h_unfolded_prepared = false;
   _arom_prepared = false;
   _aromatized = false;
   mode = mode_;
}

IndigoMoleculeSubstructureMatcher::~IndigoMoleculeSubstructureMatcher ()
{
}

const char * IndigoMoleculeSubstructureMatcher::debugInfo ()
{
   return "<molecule substructure matcher>";
}

void IndigoMoleculeSubstructureMatcher::ignoreAtom (int atom_index)
{
   _ignored_atoms.push(atom_index);
}

void IndigoMoleculeSubstructureMatcher::unignoreAtom (int atom_index)
{
   int pos = _ignored_atoms.find(atom_index);
   if (pos == -1)
      throw IndigoError("Atom with index %d wasn't ignored", atom_index);
   _ignored_atoms.remove(pos);
}

void IndigoMoleculeSubstructureMatcher::unignoreAllAtoms ()
{
   _ignored_atoms.clear();
}

IndigoMoleculeSubstructureMatchIter*
   IndigoMoleculeSubstructureMatcher::iterateQueryMatches (IndigoObject &query_object,
      bool embedding_edges_uniqueness, bool find_unique_embeddings, bool for_iteration, 
      int max_embeddings)
{
   QueryMolecule &query = query_object.getQueryMolecule();

   Molecule *target_prepared;
   Array<int> *mapping;
   bool *prepared;
   MoleculeAtomNeighbourhoodCounters *nei_counters;
   
   // If max_embeddings is 1 then it is only check for substructure 
   // and not enumeration of number of matches
   if (MoleculeSubstructureMatcher::shouldUnfoldTargetHydrogens(query, max_embeddings != 1))
   {
      if (!_arom_h_unfolded_prepared)
         _target_arom_h_unfolded.clone(target, &_mapping_arom_h_unfolded, 0);

      target_prepared = &_target_arom_h_unfolded;
      mapping = &_mapping_arom_h_unfolded;
      prepared = &_arom_h_unfolded_prepared;
      nei_counters = &_nei_counters_h_unfolded;
   }
   else
   {
      if (!_arom_prepared)
         _target_arom.clone(target, &_mapping_arom, 0);
      target_prepared = &_target_arom;
      mapping = &_mapping_arom;
      prepared = &_arom_prepared;
      nei_counters = &_nei_counters;
   }
   if (!*prepared)
   {
      if (!target.isAromatized())
         target_prepared->aromatize();
      nei_counters->calculate(*target_prepared);
      *prepared = true;
   }

   AutoPtr<IndigoMoleculeSubstructureMatchIter>
      iter(new IndigoMoleculeSubstructureMatchIter(*target_prepared, query, target,
                                                  (mode == RESONANCE), max_embeddings != 1));
   
   if (query_object.type == IndigoObject::QUERY_MOLECULE)
   {
      IndigoQueryMolecule &qm_object = (IndigoQueryMolecule &)query_object;
      iter->matcher.setNeiCounters(&qm_object.getNeiCounters(), nei_counters);
   }

   iter->matcher.find_unique_embeddings = find_unique_embeddings;
   iter->matcher.find_unique_by_edges = embedding_edges_uniqueness;
   iter->matcher.save_for_iteration = for_iteration;

   for (int i = 0; i < _ignored_atoms.size(); i++)
      iter->matcher.ignoreTargetAtom(mapping->at(_ignored_atoms[i]));

   iter->matcher.restore_unfolded_h = false;
   iter->mapping.copy(*mapping);
   iter->max_embeddings = max_embeddings;

   return iter.release();
}

bool IndigoMoleculeSubstructureMatcher::findTautomerMatch (
               QueryMolecule &query, PtrArray<TautomerRule> &tautomer_rules, Array<int> &mapping_out)
{
   // shameless copy-paste from the method above
   bool *prepared;
   Molecule *target_prepared;
   Array<int> *mapping;
   if (MoleculeSubstructureMatcher::shouldUnfoldTargetHydrogens(query, false))
   {
      if (!_arom_h_unfolded_prepared)
         _target_arom_h_unfolded.clone(target, &_mapping_arom_h_unfolded, 0);
      target_prepared = &_target_arom_h_unfolded;
      mapping = &_mapping_arom_h_unfolded;
      prepared = &_arom_h_unfolded_prepared;
   }
   else
   {
      if (!_arom_prepared)
         _target_arom.clone(target, &_mapping_arom, 0);
      target_prepared = &_target_arom;
      mapping = &_mapping_arom;
      prepared = &_arom_prepared;
   }
   if (!target.isAromatized() && !*prepared)
      target_prepared->aromatize();
   *prepared = true;
   
   if (tau_matcher.get() == 0)
   {
      bool substructure = true;
      tau_matcher.create(*target_prepared, substructure);
   }

   tau_matcher->setRulesList(&tautomer_rules);
   tau_matcher->setRules(tau_params.conditions, tau_params.force_hydrogens, tau_params.ring_chain);
   tau_matcher->setQuery(query);
   if (!tau_matcher->find())
      return false;

   mapping_out.clear_resize(query.vertexEnd());
   mapping_out.fffill();

   const int *qm = tau_matcher->getQueryMapping();

   for (int i = query.vertexBegin(); i != query.vertexEnd(); i = query.vertexNext(i))
      if (qm[i] >= 0)
         mapping_out[i] = mapping->at(qm[i]);

   return true;
}

CEXPORT int indigoSubstructureMatcher (int target, const char *mode_str)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(target);

      if (IndigoBaseMolecule::is(obj))
      {
         Molecule &mol = obj.getMolecule();
         int mode = IndigoMoleculeSubstructureMatcher::NORMAL;
         IndigoTautomerParams tau_params;

         if (mode_str != 0 && *mode_str != 0)
         {
            if (_indigoParseTautomerFlags(mode_str, tau_params))
               mode = IndigoMoleculeSubstructureMatcher::TAUTOMER;
            else if (strcasecmp(mode_str, "RES") == 0)
               mode = IndigoMoleculeSubstructureMatcher::RESONANCE;
            else
               throw IndigoError("indigoSubstructureMatcher(): unsupported mode %s", mode_str);
         }

         AutoPtr<IndigoMoleculeSubstructureMatcher> matcher(
            new IndigoMoleculeSubstructureMatcher(mol, mode));

         if (mode == IndigoMoleculeSubstructureMatcher::TAUTOMER)
            matcher->tau_params = tau_params;

         return self.addObject(matcher.release());
      }
      if (IndigoBaseReaction::is(obj))
      {
         Reaction &rxn = obj.getReaction();
         bool daylight_aam = false;

         if (mode_str != 0 && *mode_str != 0)
         {
            if (strcasecmp(mode_str, "DAYLIGHT-AAM") == 0)
               daylight_aam = true;
            else
               throw IndigoError("reaction substructure matcher: unknown mode %s", mode_str);
         }

         AutoPtr<IndigoReactionSubstructureMatcher> matcher(new IndigoReactionSubstructureMatcher(rxn));
         matcher->daylight_aam = daylight_aam;
         return self.addObject(matcher.release());
      }
      throw IndigoError("indigoSubstructureMatcher(): %s is neither a molecule not a reaction",
                        obj.debugInfo());
   }
   INDIGO_END(-1)
}

IndigoMoleculeSubstructureMatcher & IndigoMoleculeSubstructureMatcher::cast (IndigoObject &obj)
{
   if (obj.type != IndigoObject::MOLECULE_SUBSTRUCTURE_MATCHER)
      throw IndigoError("%s is not a matcher object", obj.debugInfo());

   return (IndigoMoleculeSubstructureMatcher &)obj;
}

IndigoMoleculeSubstructureMatchIter * IndigoMoleculeSubstructureMatcher::getMatchIterator (
      Indigo &self, int query, bool for_iteration, int max_embeddings)
{
   return iterateQueryMatches(self.getObject(query),
           self.embedding_edges_uniqueness, self.find_unique_embeddings, 
           for_iteration, max_embeddings);
}

CEXPORT int indigoIgnoreAtom (int target_matcher, int atom_object)
{
   INDIGO_BEGIN
   {
      IndigoMoleculeSubstructureMatcher &matcher =
              IndigoMoleculeSubstructureMatcher::cast(self.getObject(target_matcher));

      IndigoAtom &ia = IndigoAtom::cast(self.getObject(atom_object));
      matcher.ignoreAtom(ia.idx);
      return 0;
   }
   INDIGO_END(-1)
}

// Ignore target atom in the substructure matcher
CEXPORT int indigoUnignoreAtom (int target_matcher, int atom_object)
{
   INDIGO_BEGIN
   {
      IndigoMoleculeSubstructureMatcher &matcher =
              IndigoMoleculeSubstructureMatcher::cast(self.getObject(target_matcher));

      IndigoAtom &ia = IndigoAtom::cast(self.getObject(atom_object));
      matcher.unignoreAtom(ia.idx);
      return 0;
   }
   INDIGO_END(-1)
}

CEXPORT int indigoUnignoreAllAtoms (int target_matcher)
{
   INDIGO_BEGIN
   {
      IndigoMoleculeSubstructureMatcher &matcher =
              IndigoMoleculeSubstructureMatcher::cast(self.getObject(target_matcher));
      matcher.unignoreAllAtoms();
      return 0;
   }
   INDIGO_END(-1)
}

CEXPORT int indigoMatch (int target_matcher, int query)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(target_matcher);

      if (obj.type == IndigoObject::MOLECULE_SUBSTRUCTURE_MATCHER)
      {
         IndigoMoleculeSubstructureMatcher &matcher = IndigoMoleculeSubstructureMatcher::cast(obj);

         if (matcher.mode == IndigoMoleculeSubstructureMatcher::TAUTOMER)
         {
            QueryMolecule &qmol = self.getObject(query).getQueryMolecule();
            AutoPtr<IndigoMapping> mptr(new IndigoMapping(qmol, matcher.target));
            if (!matcher.findTautomerMatch(qmol, self.tautomer_rules, mptr->mapping))
               return 0;

            return self.addObject(mptr.release());
         }
         else // NORMAL or RESONANCE
         {
            AutoPtr<IndigoMoleculeSubstructureMatchIter>
               match_iter(matcher.getMatchIterator(self, query, false, 1));

            match_iter->matcher.find_unique_embeddings = false;

            if (!match_iter->hasNext())
               return 0;
            return self.addObject(match_iter->next());
         }
      }
      if (obj.type == IndigoObject::REACTION_SUBSTRUCTURE_MATCHER)
      {
         IndigoReactionSubstructureMatcher &matcher = IndigoReactionSubstructureMatcher::cast(obj);
         QueryReaction &qrxn = self.getObject(query).getQueryReaction();
         int i, j;

         ReactionAutomapper ram(qrxn);
         ram.correctReactingCenters(true);
         
         for (i = qrxn.begin(); i != qrxn.end(); i = qrxn.next(i))
            if (MoleculeSubstructureMatcher::shouldUnfoldTargetHydrogens(qrxn.getQueryMolecule(i), false))
               break;

         if (i != qrxn.end())
         {
            matcher.target.unfoldHydrogens();
            // expand mappings to include unfolded hydrogens
            for (i = matcher.target.begin(); i != matcher.target.end(); i = matcher.target.next(i))
               matcher.mappings[i].expandFill(matcher.target.getBaseMolecule(i).vertexEnd(), -1);
         }

         if (matcher.matcher.get() == 0)
            matcher.matcher.create(matcher.target);

         matcher.matcher->use_daylight_aam_mode = matcher.daylight_aam;
         matcher.matcher->setQuery(qrxn);
         if (!matcher.matcher->find())
            return 0;

         AutoPtr<IndigoReactionMapping> mapping(new IndigoReactionMapping(qrxn, matcher.original_target));
         mapping->mol_mapping.clear_resize(qrxn.end());
         mapping->mol_mapping.fffill();
         mapping->mappings.expand(qrxn.end());

         for (i = qrxn.begin(); i != qrxn.end(); i = qrxn.next(i))
         {
            if (qrxn.getSideType(i) == BaseReaction::CATALYST)
               continue;
            int tmol_idx = matcher.matcher->getTargetMoleculeIndex(i);
            mapping->mol_mapping[i] = matcher.mol_mapping[tmol_idx];
            BaseMolecule &qm = qrxn.getBaseMolecule(i);
            mapping->mappings[i].clear_resize(qm.vertexEnd());
            mapping->mappings[i].fffill();
            for (j = qm.vertexBegin(); j != qm.vertexEnd(); j = qm.vertexNext(j))
            {
               int mapped = matcher.matcher->getQueryMoleculeMapping(i)[j];

               if (mapped >= 0) // hydrogens are ignored
                  mapping->mappings[i][j] = matcher.mappings[tmol_idx][mapped];
            }
         }

         return self.addObject(mapping.release());

      }
      throw IndigoError("indigoIterateMatches(): expected a matcher, got %s", obj.debugInfo());

   }
   INDIGO_END(-1)
}

int indigoCountMatches (int target_matcher, int query)
{
   return indigoCountMatchesWithLimit(target_matcher, query, 0);
}

CEXPORT int indigoCountMatchesWithLimit (int target_matcher, int query, int embeddings_limit)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(target_matcher);

      if (obj.type == IndigoObject::MOLECULE_SUBSTRUCTURE_MATCHER)
      {
         IndigoMoleculeSubstructureMatcher &matcher = IndigoMoleculeSubstructureMatcher::cast(obj);

         if (matcher.mode == IndigoMoleculeSubstructureMatcher::TAUTOMER)
            throw IndigoError("count matches: not supported in this mode");

         if (embeddings_limit > self.max_embeddings)
            throw IndigoError("count matches: embeddings limit is more then maximum "
               "allowed embeddings specified by options");

         AutoPtr<IndigoMoleculeSubstructureMatchIter>
            match_iter(matcher.getMatchIterator(self, query, false, self.max_embeddings));

         return match_iter->countMatches(embeddings_limit);
      }
      if (obj.type == IndigoObject::REACTION_SUBSTRUCTURE_MATCHER)
         throw IndigoError("count matches: can not work with reactions");
      throw IndigoError("count matches: expected a matcher, got %s", obj.debugInfo());
   }
   INDIGO_END(-1)
}

int indigoIterateMatches (int target_matcher, int query)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(target_matcher);

      if (obj.type == IndigoObject::MOLECULE_SUBSTRUCTURE_MATCHER)
      {
         IndigoMoleculeSubstructureMatcher &matcher = IndigoMoleculeSubstructureMatcher::cast(obj);

         if (matcher.mode == IndigoMoleculeSubstructureMatcher::TAUTOMER)
            throw IndigoError("indigoIterateMatches(): not supported in this mode");

         AutoPtr<IndigoMoleculeSubstructureMatchIter>
            match_iter(matcher.getMatchIterator(self, query, true, self.max_embeddings));

         return self.addObject(match_iter.release());
      }
      if (obj.type == IndigoObject::REACTION_SUBSTRUCTURE_MATCHER)
         throw IndigoError("indigoIterateMatches(): can not work with reactions");
      throw IndigoError("indigoIterateMatches(): expected a matcher, got %s", obj.debugInfo());
   }
   INDIGO_END(-1)
}

const char * IndigoReactionSubstructureMatcher::debugInfo ()
{
   return "<reaction substructure matcher>";
}

IndigoReactionSubstructureMatcher::IndigoReactionSubstructureMatcher (Reaction &target_) :
   IndigoObject(REACTION_SUBSTRUCTURE_MATCHER),
   original_target(target_)
{
   target.clone(target_, &mol_mapping, &mappings, 0);

   target.aromatize();
   daylight_aam = false;
}

IndigoReactionSubstructureMatcher::~IndigoReactionSubstructureMatcher ()
{
}

IndigoReactionSubstructureMatcher & IndigoReactionSubstructureMatcher::cast (IndigoObject &obj)
{
   if (obj.type != IndigoObject::REACTION_SUBSTRUCTURE_MATCHER)
      throw IndigoError("%s is not a reaction matcher object", obj.debugInfo());

   return (IndigoReactionSubstructureMatcher &)obj;
}
