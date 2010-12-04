/****************************************************************************
 * Copyright (C) 2009-2010 GGA Software Services LLC
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

#include "molecule/molecule.h"
#include "molecule/query_molecule.h"
#include "base_cpp/output.h"
#include "layout/metalayout.h"
#include "render_context.h"
#include "render_internal.h"
#include "render_item_aux.h"

using namespace indigo;

RenderItemAuxiliary::RenderItemAuxiliary (RenderItemFactory& factory) : 
   RenderItemBase(factory)
{
}

RenderItemAuxiliary::~RenderItemAuxiliary ()
{
}

void RenderItemAuxiliary::_drawText ()
{                                  
   TextItem ti;
   ti.text.copy(text);
   ti.fontsize = FONT_SIZE_COMMENT;
   Vec2f c;
   c.scaled(size, 0.5f);
   _rc.setTextItemSize(ti, c);
   _rc.drawTextItemText(ti);
}

void RenderItemAuxiliary::_drawRGroupLabel ()
{
   QUERY_MOL_BEGIN(mol);
   {
      MoleculeRGroups& rgs = qmol.rgroups;
      RGroup& rg = rgs.getRGroup(rLabelIdx);

      TextItem tiR;
      tiR.fontsize = FONT_SIZE_LABEL;
      tiR.color = CWC_BASE;
      bprintf(tiR.text, "R%d=", rLabelIdx); 
      _rc.setTextItemSize(tiR);
      referenceY = tiR.bbsz.y / 2;
      tiR.bbp.set(0,0);
      _rc.drawTextItemText(tiR);

      float ypos = tiR.bbp.y + tiR.bbsz.y + _settings.bondLineWidth;

      if (rg.occurrence.size() > 0)
      {
         TextItem tiOccurrence;
         tiOccurrence.fontsize = FONT_SIZE_RGROUP_LOGIC_INDEX;
         tiOccurrence.color = CWC_BASE;
         ArrayOutput output(tiOccurrence.text);
         for (int i = 0; i < rg.occurrence.size(); ++i)
         {
            int v = rg.occurrence[i];
            int a = (v >> 16) & 0xFFFF;
            int b = v & 0xFFFF;
            if (i > 0)
               output.printf(", ");
            if (a == b)
               output.printf("%d", a);
            else if (a == 0)
               output.printf("<%d", b+1);
            else if (b == 0xFFFF)
               output.printf(">%d", a-1);
            else
               output.printf("%d-%d", a, b);
         }
         output.writeByte(0);

         _rc.setTextItemSize(tiOccurrence);
         tiOccurrence.bbp.set(0, ypos);
         _rc.drawTextItemText(tiOccurrence);

         ypos += tiOccurrence.bbsz.y + _settings.bondLineWidth;
      }

      if (rg.rest_h > 0)
      {
         TextItem tiRestH;
         tiRestH.fontsize = FONT_SIZE_RGROUP_LOGIC_INDEX;
         tiRestH.color = CWC_BASE;
         bprintf(tiRestH.text, "RestH");

         _rc.setTextItemSize(tiRestH);
         tiRestH.bbp.set(0, ypos);
         _rc.drawTextItemText(tiRestH);
      }
   }
   QUERY_MOL_END;
}

void RenderItemAuxiliary::_drawRIfThen ()
{
   QUERY_MOL_BEGIN(mol);
   {
      MoleculeRGroups& rgs = qmol.rgroups;

      float ypos = 0;
      for (int i = 1; i <= rgs.getRGroupCount(); ++i)
      {
         const RGroup& rg = rgs.getRGroup(i);
         if (rg.if_then > 0)
         {
            TextItem tiIfThen;
            tiIfThen.fontsize = FONT_SIZE_RGROUP_LOGIC;
            tiIfThen.color = CWC_BASE;
            bprintf(tiIfThen.text, "IF R%d THEN R%d", i, rg.if_then); 
            _rc.setTextItemSize(tiIfThen);
            tiIfThen.bbp.set(0, ypos);
            _rc.drawTextItemText(tiIfThen);

            ypos += tiIfThen.bbsz.y + _settings.rGroupIfThenInterval;
         }
      }
   }
   QUERY_MOL_END;
}

void RenderItemAuxiliary::render ()
{
   _rc.translate(-origin.x, -origin.y);
   switch (type) {
      case AUX_TEXT:
         return;
      case AUX_RXN_PLUS:
         return;
      case AUX_RXN_ARROW:
         return;
      case AUX_RGROUP_LABEL:
         _drawRGroupLabel();
         return;
      case AUX_RGROUP_IFTHEN:
         _drawRIfThen();
         return;
      default:
         throw Error("Item type not set or invalid");
   }
}