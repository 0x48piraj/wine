/*
 * Implementation of the Microsoft Installer (msi.dll)
 *
 * Copyright 2002 Mike McCormack for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wine/debug.h"
#include "msi.h"
#include "msiquery.h"
#include "objbase.h"
#include "objidl.h"
#include "msipriv.h"
#include "winnls.h"

#include "query.h"

WINE_DEFAULT_DEBUG_CHANNEL(msi);

typedef struct tagMSICOLUMNINFO
{
    LPWSTR tablename;
    UINT   number;
    LPWSTR colname;
    UINT   type;
    UINT   offset;
} MSICOLUMNINFO;

struct tagMSITABLE
{
    USHORT *data;
    UINT size;
    UINT ref_count;
    struct tagMSITABLE *next;
    struct tagMSITABLE *prev;
    WCHAR name[1];
} ;

#define MAX_STREAM_NAME 0x1f

static int utf2mime(int x)
{
    if( (x>='0') && (x<='9') )
        return x-'0';
    if( (x>='A') && (x<='Z') )
        return x-'A'+10;
    if( (x>='a') && (x<='z') )
        return x-'a'+10+26;
    if( x=='.' )
        return 10+26+26;
    if( x=='_' )
        return 10+26+26+1;
    return -1;
}

static BOOL encode_streamname(BOOL bTable, LPCWSTR in, LPWSTR out)
{
    DWORD count = MAX_STREAM_NAME;
    DWORD ch, next;

    if( bTable )
    {
         *out++ = 0x4840;
         count --;
    }
    while( count -- ) 
    {
        ch = *in++;
        if( !ch )
        {
            *out = ch;
            return TRUE;
        }
        if( ( ch < 0x80 ) && ( utf2mime(ch) >= 0 ) )
        {
            ch = utf2mime(ch) + 0x4800;
            next = *in;
            if( next && (next<0x80) )
            {
                next = utf2mime(next);
                if( next >= 0  )
                {
                     next += 0x3ffffc0;
                     ch += (next<<6);
                     in++;
                }
            }
        }
        *out++ = ch;
    }
    return FALSE;
}

#if 0
static int mime2utf(int x)
{
    if( x<10 )
        return x + '0';
    if( x<(10+26))
        return x - 10 + 'A';
    if( x<(10+26+26))
        return x - 10 - 26 + 'a';
    if( x == (10+26+26) )
        return '.';
    return '_';
}

static BOOL decode_streamname(LPWSTR in, LPWSTR out)
{
    WCHAR ch;
    DWORD count = 0;

    while ( (ch = *in++) )
    {
        if( (ch >= 0x3800 ) && (ch < 0x4840 ) )
        {
            if( ch >= 0x4800 )
                ch = mime2utf(ch-0x4800);
            else
            {
                ch -= 0x3800;
                *out++ = mime2utf(ch&0x3f);
                count++;
                ch = mime2utf((ch>>6)&0x3f);
            }
        }
        *out++ = ch;
        count++;
    }
    *out = 0;
    return count;
}
#endif

static BOOL read_stream_data( IStorage *stg, LPWSTR stname,
                              USHORT **pdata, UINT *psz )
{
    HRESULT r;
    UINT ret = ERROR_FUNCTION_FAILED;
    VOID *data;
    ULONG sz, count;
    IStream *stm = NULL;
    STATSTG stat;

    r = IStorage_OpenStream(stg, stname, NULL, 
            STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stm);
    if( FAILED( r ) )
    {
        WARN("open stream failed r = %08lx - empty table?\n",r);
        return ret;
    }

    r = IStream_Stat(stm, &stat, STATFLAG_NONAME );
    if( FAILED( r ) )
    {
        ERR("open stream failed r = %08lx!\n",r);
        goto end;
    }

    if( stat.cbSize.QuadPart >> 32 )
    {
        ERR("Too big!\n");
        goto end;
    }
        
    sz = stat.cbSize.QuadPart;
    data = HeapAlloc( GetProcessHeap(), 0, sz );
    if( !data )
    {
        ERR("couldn't allocate memory r=%08lx!\n",r);
        ret = ERROR_NOT_ENOUGH_MEMORY;
        goto end;
    }
        
    r = IStream_Read(stm, data, sz, &count );
    if( FAILED( r ) || ( count != sz ) )
    {
        HeapFree( GetProcessHeap(), 0, data );
        ERR("read stream failed r = %08lx!\n",r);
        goto end;
    }

    *pdata = data;
    *psz = sz;
    ret = ERROR_SUCCESS;

end:
    IStream_Release( stm );

    return ret;
}

UINT read_table_from_storage(IStorage *stg, LPCWSTR name, MSITABLE **ptable)
{
    WCHAR buffer[0x20];
    MSITABLE *t;

    TRACE("%s -> %s\n",debugstr_w(name),debugstr_w(buffer));

    /* non-existing tables should be interpretted as empty tables */
    t = HeapAlloc( GetProcessHeap(), 0, 
                   sizeof (MSITABLE) + lstrlenW(name)*sizeof (WCHAR) );
    if( !t )
        return ERROR_NOT_ENOUGH_MEMORY;

    t->size = 0;
    t->data = NULL;
    lstrcpyW( t->name, name );
    t->ref_count = 1;
    *ptable = t;

    /* if we can't read the table, just assume that it's empty */
    encode_streamname(TRUE, name, buffer);
    read_stream_data( stg, buffer, &t->data, &t->size );

    return ERROR_SUCCESS;
}

/* add this table to the list of cached tables in the database */
void add_table(MSIDATABASE *db, MSITABLE *table)
{
    table->next = db->first_table;
    table->prev = NULL;
    if( db->first_table )
        db->first_table->prev = table;
    else
        db->last_table = table;
    db->first_table = table;
}
 
/* remove from the list of cached tables */
void remove_table( MSIDATABASE *db, MSITABLE *table )
{
    if( table->next )
        table->next->prev = table->prev;
    else
        db->last_table = table->prev;
    if( table->prev )
        table->prev->next = table->next;
    else
        db->first_table = table->next;
    table->next = NULL;
    table->prev = NULL;
}

void release_table( MSIDATABASE *db, MSITABLE *table )
{
    if( !table->ref_count )
        ERR("Trying to destroy table with refcount 0\n");
    table->ref_count --;
    if( !table->ref_count )
    {
        remove_table( db, table );
        HeapFree( GetProcessHeap(), 0, table->data );
        HeapFree( GetProcessHeap(), 0, table );
        TRACE("Destroyed table %s\n", debugstr_w(table->name));
    }
}

void free_cached_tables( MSIDATABASE *db )
{
    while( db->first_table )
    {
        MSITABLE *t = db->first_table;

        if ( --t->ref_count )
            ERR("table ref count not zero for %s\n", debugstr_w(t->name));
        remove_table( db, t );
        HeapFree( GetProcessHeap(), 0, t->data );
        HeapFree( GetProcessHeap(), 0, t );
    }
}

UINT find_cached_table(MSIDATABASE *db, LPCWSTR name, MSITABLE **ptable)
{
    MSITABLE *t;

    for( t = db->first_table; t; t=t->next )
    {
        if( !lstrcmpW( name, t->name ) )
        {
            *ptable = t;
            return ERROR_SUCCESS;
        }
    }

    return ERROR_FUNCTION_FAILED;
}

UINT get_table(MSIDATABASE *db, LPCWSTR name, MSITABLE **ptable)
{
    UINT r;

    *ptable = NULL;

    /* first, see if the table is cached */
    r = find_cached_table( db, name, ptable );
    if( r == ERROR_SUCCESS )
    {
        (*ptable)->ref_count++;
        return r;
    }

    r = read_table_from_storage( db->storage, name, ptable );
    if( r != ERROR_SUCCESS )
        return r;

    /* add the table to the list */
    add_table( db, *ptable );
    (*ptable)->ref_count++;

    return ERROR_SUCCESS;
}

UINT load_string_table( MSIDATABASE *db )
{
    MSITABLE *pool = NULL, *info = NULL;
    UINT r, ret = ERROR_FUNCTION_FAILED;
    DWORD i, count, offset, len;
    const WCHAR szStringData[] = { 
        '_','S','t','r','i','n','g','D','a','t','a',0 };
    const WCHAR szStringPool[] = { 
        '_','S','t','r','i','n','g','P','o','o','l',0 };

    if( db->strings )
    {
        msi_destroy_stringtable( db->strings );
        db->strings = NULL;
    }

    r = get_table( db, szStringPool, &pool );
    if( r != ERROR_SUCCESS)
        goto end;
    r = get_table( db, szStringData, &info );
    if( r != ERROR_SUCCESS)
        goto end;

    count = pool->size/4;
    db->strings = msi_init_stringtable( count );

    offset = 0;
    for( i=0; i<count; i++ )
    {
        len = pool->data[i*2];
        msi_addstring( db->strings, i, (LPSTR)info->data+offset, len, pool->data[i*2+1] );
        offset += len;
    }

    TRACE("Loaded %ld strings\n", count);

    ret = ERROR_SUCCESS;

end:
    if( info )
        release_table( db, info );
    if( pool )
        release_table( db, pool );

    return ret;
}

static LPWSTR strdupW( LPCWSTR str )
{
    UINT len = lstrlenW( str ) + 1;
    LPWSTR ret = HeapAlloc( GetProcessHeap(), 0, len*sizeof (WCHAR) );
    if( ret )
        lstrcpyW( ret, str );
    return ret;
}

static inline UINT bytes_per_column( MSICOLUMNINFO *col )
{
    if( col->type & MSITYPE_STRING )
        return 2;
    if( (col->type & 0xff) > 4 )
        ERR("Invalid column size!\n");
    return col->type & 0xff;
}

/* information for default tables */
const WCHAR szTables[]  = { '_','T','a','b','l','e','s',0 };
const WCHAR szTable[]  = { 'T','a','b','l','e',0 };
const WCHAR szName[]    = { 'N','a','m','e',0 };
const WCHAR szColumns[] = { '_','C','o','l','u','m','n','s',0 };
const WCHAR szColumn[]  = { 'C','o','l','u','m','n',0 };
const WCHAR szNumber[]  = { 'N','u','m','b','e','r',0 };
const WCHAR szType[]    = { 'T','y','p','e',0 };

struct standard_table {
    LPCWSTR tablename;
    LPCWSTR columnname;
    UINT number;
    UINT type;
} MSI_standard_tables[] =
{
  { szTables,  szName,   1, MSITYPE_VALID | MSITYPE_STRING | 32},
  { szColumns, szTable,  1, MSITYPE_VALID | MSITYPE_STRING | 32},
  { szColumns, szNumber, 2, MSITYPE_VALID | 2},
  { szColumns, szName,   3, MSITYPE_VALID | MSITYPE_STRING | 32},
  { szColumns, szType,   4, MSITYPE_VALID | 2},
};

#define STANDARD_TABLE_COUNT \
     (sizeof(MSI_standard_tables)/sizeof(struct standard_table))

UINT get_defaulttablecolumns( LPCWSTR szTable, MSICOLUMNINFO *colinfo, UINT *sz)
{
    DWORD i, n=0;

    for(i=0; i<STANDARD_TABLE_COUNT; i++)
    {
        if( lstrcmpW( szTable, MSI_standard_tables[i].tablename ) )
            continue;
        if(colinfo && (n < *sz) )
        {
            colinfo[n].tablename = strdupW(MSI_standard_tables[i].tablename);
            colinfo[n].colname = strdupW(MSI_standard_tables[i].columnname);
            colinfo[n].number = MSI_standard_tables[i].number;
            colinfo[n].type = MSI_standard_tables[i].type;
            /* ERR("Table %s has column %s\n",debugstr_w(colinfo[n].tablename),
                    debugstr_w(colinfo[n].colname)); */
            if( n )
                colinfo[n].offset = colinfo[n-1].offset
                                  + bytes_per_column( &colinfo[n-1] );
            else
                colinfo[n].offset = 0;
        }
        n++;
        if( colinfo && (n >= *sz) )
            break;
    }
    *sz = n;
    return ERROR_SUCCESS;
}

LPWSTR MSI_makestring( MSIDATABASE *db, UINT stringid)
{
    UINT sz=0, r;
    LPWSTR str;

    r = msi_id2string( db->strings, stringid, NULL, &sz );
    if( r != ERROR_SUCCESS )
        return NULL;
    sz ++; /* space for NUL char */
    str = HeapAlloc( GetProcessHeap(), 0, sz*sizeof (WCHAR));
    if( !str )
        return str;
    r = msi_id2string( db->strings, stringid, str, &sz );
    if( r == ERROR_SUCCESS )
        return str;
    HeapFree(  GetProcessHeap(), 0, str );
    return NULL;
}

UINT get_tablecolumns( MSIDATABASE *db, 
       LPCWSTR szTableName, MSICOLUMNINFO *colinfo, UINT *sz)
{
    UINT r, i, n=0, table_id, count, maxcount = *sz;
    MSITABLE *table = NULL;
    const WCHAR szColumns[] = { '_','C','o','l','u','m','n','s',0 };

    /* first check if there is a default table with that name */
    r = get_defaulttablecolumns( szTableName, colinfo, sz );
    if( ( r == ERROR_SUCCESS ) && *sz )
        return r;

    r = get_table( db, szColumns, &table);
    if( r != ERROR_SUCCESS )
    {
        ERR("table %s not available\n", debugstr_w(szColumns));
        return r;
    }

    /* convert table and column names to IDs from the string table */
    r = msi_string2id( db->strings, szTableName, &table_id );
    if( r != ERROR_SUCCESS )
    {
        release_table( db, table );
        ERR("Couldn't find id for %s\n", debugstr_w(szTableName));
        return r;
    }

    TRACE("Table id is %d\n", table_id);

    count = table->size/8;
    for( i=0; i<count; i++ )
    {
        if( table->data[ i ] != table_id )
            continue;
        if( colinfo )
        {
            UINT id = table->data[ i + count*2 ];
            colinfo[n].tablename = MSI_makestring( db, table_id );
            colinfo[n].number = table->data[ i + count ] - (1<<15);
            colinfo[n].colname = MSI_makestring( db, id );
            colinfo[n].type = table->data[ i + count*3 ];
            /* this assumes that columns are in order in the table */
            if( n )
                colinfo[n].offset = colinfo[n-1].offset
                                  + bytes_per_column( &colinfo[n-1] );
            else
                colinfo[n].offset = 0;
            TRACE("table %s column %d is [%s] (%d) with type %08x "
                  "offset %d at row %d\n", debugstr_w(szTableName),
                   colinfo[n].number, debugstr_w(colinfo[n].colname),
                   id, colinfo[n].type, colinfo[n].offset, i);
            if( n != (colinfo[n].number-1) )
            {
                ERR("oops. data in the _Columns table isn't in the right "
                    "order for table %s\n", debugstr_w(szTableName));
                return ERROR_FUNCTION_FAILED;
            }
        }
        n++;
        if( colinfo && ( n >= maxcount ) )
            break;
    }
    *sz = n;

    release_table( db, table );

    return ERROR_SUCCESS;
}

/* try to find the table name in the _Tables table */
BOOL TABLE_Exists( MSIDATABASE *db, LPWSTR name )
{
    const WCHAR szTables[] = { '_','T','a','b','l','e','s',0 };
    const WCHAR szColumns[] = { '_','C','o','l','u','m','n','s',0 };
    UINT r, table_id = 0, i, count;
    MSITABLE *table = NULL;

    if( !lstrcmpW( name, szTables ) )
        return TRUE;
    if( !lstrcmpW( name, szColumns ) )
        return TRUE;

    r = msi_string2id( db->strings, name, &table_id );
    if( r != ERROR_SUCCESS )
    {
        ERR("Couldn't find id for %s\n", debugstr_w(name));
        return FALSE;
    }

    r = get_table( db, szTables, &table);
    if( r != ERROR_SUCCESS )
    {
        ERR("table %s not available\n", debugstr_w(szTables));
        return FALSE;
    }

    count = table->size/2;
    for( i=0; i<count; i++ )
        if( table->data[ i ] == table_id )
            break;

    release_table( db, table );

    if (i!=count)
        return TRUE;

    return FALSE;
}

/* below is the query interface to a table */

typedef struct tagMSITABLEVIEW
{
    MSIVIEW        view;
    MSIDATABASE   *db;
    MSITABLE      *table;
    MSICOLUMNINFO *columns;
    UINT           num_cols;
    UINT           row_size;
    WCHAR          name[1];
} MSITABLEVIEW;


static UINT TABLE_fetch_int( struct tagMSIVIEW *view, UINT row, UINT col, UINT *val )
{
    MSITABLEVIEW *tv = (MSITABLEVIEW*)view;
    UINT offset, num_rows, n;

    if( !tv->table )
        return ERROR_INVALID_PARAMETER;

    if( (col==0) || (col>tv->num_cols) )
        return ERROR_INVALID_PARAMETER;

    /* how many rows are there ? */
    num_rows = tv->table->size / tv->row_size;
    if( row >= num_rows )
        return ERROR_NO_MORE_ITEMS;

    if( tv->columns[col-1].offset >= tv->row_size )
    {
        ERR("Stuffed up %d >= %d\n", tv->columns[col-1].offset, tv->row_size );
        ERR("%p %p\n", tv, tv->columns );
        return ERROR_FUNCTION_FAILED;
    }

    offset = row + (tv->columns[col-1].offset/2) * num_rows;
    n = bytes_per_column( &tv->columns[col-1] );
    switch( n )
    {
    case 4:
        offset = row*2 + (tv->columns[col-1].offset/2) * num_rows;
        *val = tv->table->data[offset] + (tv->table->data[offset + 1] << 16);
        break;
    case 2:
        offset = row + (tv->columns[col-1].offset/2) * num_rows;
        *val = tv->table->data[offset];
        break;
    default:
        ERR("oops! what is %d bytes per column?\n", n );
        return ERROR_FUNCTION_FAILED;
    }

    TRACE("Data [%d][%d] = %d \n", row, col, *val );

    return ERROR_SUCCESS;
}

static UINT TABLE_execute( struct tagMSIVIEW *view, MSIHANDLE record )
{
    MSITABLEVIEW *tv = (MSITABLEVIEW*)view;
    UINT r;

    TRACE("%p %ld\n", tv, record);

    if( tv->table )
        return ERROR_FUNCTION_FAILED;

    r = get_table( tv->db, tv->name, &tv->table );
    if( r != ERROR_SUCCESS )
        return r;
    
    return ERROR_SUCCESS;
}

static UINT TABLE_close( struct tagMSIVIEW *view )
{
    MSITABLEVIEW *tv = (MSITABLEVIEW*)view;

    TRACE("%p\n", view );

    if( !tv->table )
        return ERROR_FUNCTION_FAILED;

    release_table( tv->db, tv->table );
    tv->table = NULL;
    
    return ERROR_SUCCESS;
}

static UINT TABLE_get_dimensions( struct tagMSIVIEW *view, UINT *rows, UINT *cols)
{
    MSITABLEVIEW *tv = (MSITABLEVIEW*)view;

    TRACE("%p %p %p\n", view, rows, cols );

    if( cols )
        *cols = tv->num_cols;
    if( rows )
    {
        if( !tv->table )
            return ERROR_INVALID_PARAMETER;
        *rows = tv->table->size / tv->row_size;
    }

    return ERROR_SUCCESS;
}

static UINT TABLE_get_column_info( struct tagMSIVIEW *view,
                UINT n, LPWSTR *name, UINT *type )
{
    MSITABLEVIEW *tv = (MSITABLEVIEW*)view;

    TRACE("%p %d %p %p\n", tv, n, name, type );

    if( ( n == 0 ) || ( n > tv->num_cols ) )
        return ERROR_INVALID_PARAMETER;

    if( name )
    {
        *name = strdupW( tv->columns[n-1].colname );
        if( !*name )
            return ERROR_FUNCTION_FAILED;
    }
    if( type )
        *type = tv->columns[n-1].type;

    return ERROR_SUCCESS;
}

static UINT TABLE_modify( struct tagMSIVIEW *view, MSIMODIFY eModifyMode, MSIHANDLE hrec)
{
    FIXME("%p %d %ld\n", view, eModifyMode, hrec );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static UINT TABLE_delete( struct tagMSIVIEW *view )
{
    MSITABLEVIEW *tv = (MSITABLEVIEW*)view;

    TRACE("%p\n", view );

    if( tv->table )
        release_table( tv->db, tv->table );
    tv->table = NULL;

    if( tv->columns )
    {
        UINT i;
        for( i=0; i<tv->num_cols; i++)
        {
            HeapFree( GetProcessHeap(), 0, tv->columns[i].colname );
            HeapFree( GetProcessHeap(), 0, tv->columns[i].tablename );
        }
        HeapFree( GetProcessHeap(), 0, tv->columns );
    }
    tv->columns = NULL;

    HeapFree( GetProcessHeap(), 0, tv );

    return ERROR_SUCCESS;
}


MSIVIEWOPS table_ops =
{
    TABLE_fetch_int,
    TABLE_execute,
    TABLE_close,
    TABLE_get_dimensions,
    TABLE_get_column_info,
    TABLE_modify,
    TABLE_delete
};

UINT TABLE_CreateView( MSIDATABASE *db, LPWSTR name, MSIVIEW **view )
{
    MSITABLEVIEW *tv ;
    UINT r, sz, column_count;
    MSICOLUMNINFO *columns, *last_col;

    TRACE("%p %s %p\n", db, debugstr_w(name), view );

    /* get the number of columns in this table */
    column_count = 0;
    r = get_tablecolumns( db, name, NULL, &column_count );
    if( r != ERROR_SUCCESS )
        return r;

    /* if there's no columns, there's no table */
    if( column_count == 0 )
        return ERROR_INVALID_PARAMETER;

    TRACE("Table found\n");

    sz = sizeof *tv + lstrlenW(name)*sizeof name[0] ;
    tv = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sz );
    if( !tv )
        return ERROR_FUNCTION_FAILED;
    
    columns = HeapAlloc( GetProcessHeap(), 0, column_count*sizeof (MSICOLUMNINFO));
    if( !columns )
    {
        HeapFree( GetProcessHeap(), 0, tv );
        return ERROR_FUNCTION_FAILED;
    }

    r = get_tablecolumns( db, name, columns, &column_count );
    if( r != ERROR_SUCCESS )
    {
        HeapFree( GetProcessHeap(), 0, columns );
        HeapFree( GetProcessHeap(), 0, tv );
        return ERROR_FUNCTION_FAILED;
    }

    TRACE("Table has %d columns\n", column_count);

    last_col = &columns[column_count-1];

    /* fill the structure */
    tv->view.ops = &table_ops;
    tv->db = db;
    tv->columns = columns;
    tv->num_cols = column_count;
    tv->table = NULL;
    tv->row_size = last_col->offset + bytes_per_column( last_col );

    TRACE("one row is %d bytes\n", tv->row_size );

    *view = (MSIVIEW*) tv;
    lstrcpyW( tv->name, name );

    return ERROR_SUCCESS;
}
