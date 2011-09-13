#include "bingo_pg_cursor.h"
#include "bingo_pg_common.h"
#include "pg_bingo_context.h"
#include "base_cpp/tlscont.h"
#include "base_cpp/output.h"
#include "bingo_pg_text.h"

CEXPORT {
#include "postgres.h"
#include "fmgr.h"
#include "catalog/pg_type.h"
#include "storage/itemptr.h"
#include "executor/spi.h"
}

using namespace indigo;

static int cursor_idx = 0;

BingoPgCursor::BingoPgCursor(const char *format, ...) {
   Array<char> buf;
   va_list args;
   va_start(args, format);
   ArrayOutput output(buf);
   output.vprintf(format, args);
   output.writeChar(0);
   va_end(args);

   _init(buf);
}

BingoPgCursor::BingoPgCursor(indigo::Array<char>& query_str) {
   _init(query_str);
   
}

BingoPgCursor::~BingoPgCursor() {
   /*
    * JDBC drivers workaround
    */
   BINGO_PG_TRY
   {
      Portal cur_ptr = SPI_cursor_find(_cursorName.ptr());
      if (cur_ptr != NULL)
         SPI_cursor_close((Portal) _cursorPtr);
      SPI_finish();
   }
   BINGO_PG_HANDLE(throw Error("internal error: can not close the cursor: %s", err->message));
}

bool BingoPgCursor::next() {

   BINGO_PG_TRY
   {
      SPI_cursor_fetch((Portal)_cursorPtr, true, 1);
   }
   BINGO_PG_HANDLE(throw Error("internal error: can not fetch the cursor: %s", err->message));
   
   if(SPI_processed == 0)
      return false;
   
   if(SPI_tuptable == NULL)
      return false;

   return true;
}

void BingoPgCursor::getId(int arg_idx, ItemPointerData& data) {
   BINGO_PG_TRY
   {
      Datum record = getDatum(arg_idx);
      ItemPointer tup = (ItemPointer) DatumGetPointer(record);

      int block_num = ItemPointerGetBlockNumber(tup);
      int off_num = ItemPointerGetOffsetNumber(tup);

      ItemPointerSetBlockNumber(&data, block_num);
      ItemPointerSetOffsetNumber(&data, off_num);
   }
   BINGO_PG_HANDLE(throw Error("internal error: can not get the id from the data: %s", err->message));
}
void BingoPgCursor::getText(int arg_idx, BingoPgText& data) {
   Datum record = getDatum(arg_idx);
   data.init(record);
}

uintptr_t  BingoPgCursor::getDatum(int arg_idx) {
   if(SPI_processed == 0)
      throw Error("internal error: can not get not processed tuple");

   if(SPI_tuptable == NULL)
      throw Error("internal error: can not get null tuple");

   Datum record = 0;
   
   BINGO_PG_TRY
   {
      TupleDesc tupdesc = SPI_tuptable->tupdesc;

      /*
       * Tuple index is always 0
       */
      int tuple_idx = 0;

      HeapTuple tuple = SPI_tuptable->vals[tuple_idx];
      bool isnull;

      if (arg_idx > tupdesc->natts)
         throw Error("internal error: can not get tuple was not in query %d > %d", arg_idx, tupdesc->natts);

      record = SPI_getbinval(tuple, tupdesc, arg_idx, &isnull);

      if (isnull)
         throw Error("internal error: can not get null tuple");
   }
   BINGO_PG_HANDLE(throw Error("internal error: can not get datum from the tuple: %s", err->message));
   
   return record;
}

void BingoPgCursor::_init(indigo::Array<char>& query_str) {
   Array<dword> arg_types;

   BINGO_PG_TRY
   {
      SPI_connect();
      SPIPlanPtr plan_ptr = SPI_prepare_cursor(query_str.ptr(), arg_types.size(), arg_types.ptr(), 0);

      ++cursor_idx;
      ArrayOutput cursor_name_out(_cursorName);
      cursor_name_out.printf("bingo_cursor_%d", cursor_idx);
      cursor_name_out.writeChar(0);

      _cursorPtr = SPI_cursor_open(_cursorName.ptr(), plan_ptr, 0, 0, true);
   }
   BINGO_PG_HANDLE(throw Error("internal error: can not prepare or open a cursor: %s", err->message));
}