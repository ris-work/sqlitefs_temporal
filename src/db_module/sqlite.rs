#![allow(unused_parens)]
use crate::db_module::{DBFileAttr, DEntry, DbModule};
use crate::sqerror::{Error, ErrorKind, Result};
use chrono::{DateTime, NaiveDateTime, Timelike, Utc};
use fuse::FileType;
use rusqlite;
use rusqlite::types::ToSql;
use rusqlite::{params, Connection, Statement};
use std::path::Path;
use std::time::SystemTime;

const DB_IFIFO: u32 = 0o0_010_000;
const DB_IFCHR: u32 = 0o0_020_000;
const DB_IFDIR: u32 = 0o0_040_000;
const DB_IFBLK: u32 = 0o0_060_000;
const DB_IFREG: u32 = 0o0_100_000;
const DB_IFLNK: u32 = 0o0_120_000;
const DB_IFSOCK: u32 = 0o0_140_000;

const BLOCK_SIZE: u32 = 4096;

fn string_to_systemtime(text: String, nsec: u32) -> SystemTime {
    SystemTime::from(DateTime::<Utc>::from_utc(
        NaiveDateTime::parse_from_str(&text, "%Y-%m-%d %H:%M:%S")
            .unwrap()
            .with_nanosecond(nsec)
            .unwrap(),
        Utc,
    ))
}

fn file_type_to_const(kind: FileType) -> u32 {
    match kind {
        FileType::RegularFile => DB_IFREG,
        FileType::Socket => DB_IFSOCK,
        FileType::Directory => DB_IFDIR,
        FileType::Symlink => DB_IFLNK,
        FileType::BlockDevice => DB_IFBLK,
        FileType::CharDevice => DB_IFCHR,
        FileType::NamedPipe => DB_IFIFO,
    }
}

fn const_to_file_type(kind: u32) -> FileType {
    match kind {
        DB_IFREG => FileType::RegularFile,
        DB_IFSOCK => FileType::Socket,
        DB_IFDIR => FileType::Directory,
        DB_IFLNK => FileType::Symlink,
        DB_IFBLK => FileType::BlockDevice,
        DB_IFCHR => FileType::CharDevice,
        DB_IFIFO => FileType::NamedPipe,
        _ => FileType::RegularFile,
    }
}

/// Release all data in "inode", after "offset" byte.
fn release_data(inode: u32, offset: u32, tx: &Connection) -> Result<()> {
    if offset == 0 {
        tx.execute("DELETE FROM data WHERE file_id=$1", params![inode])?;
    } else {
        let mut block = offset / BLOCK_SIZE;
        if offset % BLOCK_SIZE != 0 {
            block = offset / BLOCK_SIZE + 1;
            let sql = "SELECT data FROM data WHERE file_id=$1 and block_num = $2";
            let mut stmt = tx.prepare(sql)?;
            let mut data: Vec<u8> = match stmt.query_row(params![inode, block], |row| row.get(0)) {
                Ok(n) => n,
                Err(err) => {
                    if err == rusqlite::Error::QueryReturnedNoRows {
                        vec![0; BLOCK_SIZE as usize]
                    } else {
                        return Err(Error::from(err));
                    }
                }
            };
            data.resize((offset % BLOCK_SIZE) as usize, 0);
            tx.execute(
                "REPLACE INTO data \
            (file_id, block_num, data)
            VALUES($1, $2, $3)",
                params![inode, block, data],
            )?;
        }
        tx.execute(
            "DELETE FROM data WHERE file_id=$1 and block_num > $2",
            params![inode, block],
        )?;
    }
    Ok(())
}

fn update_time(inode: u32, sql: &str, time: DateTime<Utc>, tx: &Connection) -> Result<()> {
    let mut stmt = tx.prepare(sql)?;
    let params = params![
        &time.format("%Y-%m-%d %H:%M:%S").to_string(),
        time.timestamp_subsec_nanos(),
        inode
    ];
    stmt.execute(params)?;
    Ok(())
}

fn update_atime(inode: u32, time: DateTime<Utc>, tx: &Connection) -> Result<()> {
    let sql = "UPDATE metadata SET atime=datetime($1), atime_nsec=$2 WHERE id=$3";
    update_time(inode, sql, time, tx)
}

fn update_mtime(inode: u32, time: DateTime<Utc>, tx: &Connection) -> Result<()> {
    let sql = "UPDATE metadata SET mtime=datetime($1), mtime_nsec=$2 WHERE id=$3";
    update_time(inode, sql, time, tx)
}

fn update_ctime(inode: u32, time: DateTime<Utc>, tx: &Connection) -> Result<()> {
    let sql = "UPDATE metadata SET ctime=datetime($1), ctime_nsec=$2 WHERE id=$3";
    update_time(inode, sql, time, tx)
}

fn add_dentry(entry: DEntry, tx: &Connection) -> Result<()> {
    let sql = "INSERT INTO dentry VALUES($1, $2, $3, $4)";
    tx.execute(
        sql,
        params![
            entry.parent_ino,
            entry.child_ino,
            file_type_to_const(entry.file_type),
            entry.filename
        ],
    )?;
    Ok(())
}

fn parse_attr(mut stmt: Statement, params: &[&dyn ToSql]) -> Result<Option<DBFileAttr>> {
    let rows = stmt.query_map(params, |row| {
        Ok(DBFileAttr {
            ino: row.get(0)?,
            size: row.get(1)?,
            blocks: row.get(17).unwrap_or(0),
            atime: string_to_systemtime(row.get(2)?, row.get(3)?),
            mtime: string_to_systemtime(row.get(4)?, row.get(5)?),
            ctime: string_to_systemtime(row.get(6)?, row.get(7)?),
            crtime: string_to_systemtime(row.get(8)?, row.get(9)?),
            kind: const_to_file_type(row.get(10)?),
            perm: row.get(11)?,
            nlink: row.get(12)?,
            uid: row.get(13)?,
            gid: row.get(14)?,
            rdev: row.get(15)?,
            flags: row.get(16)?,
        })
    })?;
    let mut attrs = Vec::new();
    for row in rows {
        attrs.push(row?);
    }
    if attrs.is_empty() {
        Ok(None)
    } else {
        Ok(Some(attrs[0]))
    }
}

fn get_inode_local(inode: u32, tx: &Connection) -> Result<Option<DBFileAttr>> {
    let sql = "SELECT \
            metadata.id,\
            metadata.size,\
            metadata.atime,\
            metadata.atime_nsec,\
            metadata.mtime,\
            metadata.mtime_nsec,\
            metadata.ctime,\
            metadata.ctime_nsec,\
            metadata.crtime,\
            metadata.crtime_nsec,\
            metadata.kind, \
            metadata.mode,\
            ncount.nlink,\
            metadata.uid,\
            metadata.gid,\
            metadata.rdev,\
            metadata.flags,\
            blocknum.block_num \
            FROM metadata \
            LEFT JOIN (SELECT count(block_num) block_num FROM data WHERE file_id=$1) AS blocknum \
            LEFT JOIN ( SELECT COUNT(child_id) nlink FROM dentry WHERE child_id=$1 GROUP BY child_id) AS ncount \
            WHERE id=$1";
    let stmt = tx.prepare(sql)?;
    let params = params![inode];
    debug!("Running {} with {}", sql, inode);
    parse_attr(stmt, params)
}
fn get_inode_local_at_time(
    inode: u32,
    tx: &Connection,
    time: String,
) -> Result<Option<DBFileAttr>> {
    debug! {"get_inode_local_at_time called: {} {}", inode, time};
    let sql = "SELECT \
            tmetadata.id,\
            tmetadata.size,\
            tmetadata.atime,\
            tmetadata.atime_nsec,\
            tmetadata.mtime,\
            tmetadata.mtime_nsec,\
            tmetadata.ctime,\
            tmetadata.ctime_nsec,\
            tmetadata.crtime,\
            tmetadata.crtime_nsec,\
            tmetadata.kind, \
            tmetadata.mode,\
            ncount.nlink,\
            tmetadata.uid,\
            tmetadata.gid,\
            tmetadata.rdev,\
            tmetadata.flags,\
            blocknum.block_num \
            FROM tmetadata \
            LEFT JOIN (SELECT count(block_num) block_num FROM tdata WHERE file_id=$1) AS blocknum \
            LEFT JOIN ( SELECT COUNT(child_id) nlink FROM tdentry WHERE child_id=$1 GROUP BY child_id) AS ncount \
            WHERE id=$1 AND ncount.nlink IS NOT NULL";
    let stmt = tx.prepare(sql)?;
    debug! {"Statement prepared for get_inode_at_time."};
    let params = params![inode];
    debug!("Running {} with {}", sql, inode);
    parse_attr(stmt, params)
}

fn get_dentry_single(parent: u32, name: &str, tx: &Connection) -> Result<Option<DEntry>> {
    let sql = "SELECT child_id, file_type FROM dentry WHERE  parent_id=$1 and name=$2";
    let mut stmt = tx.prepare(sql)?;
    let res: Option<DEntry> = match stmt.query_row(params![parent, name], |row| {
        Ok(Some(DEntry {
            parent_ino: parent,
            child_ino: row.get(0)?,
            file_type: const_to_file_type(row.get(1)?),
            filename: name.to_string(),
        }))
    }) {
        Ok(n) => n,
        Err(err) => {
            if err == rusqlite::Error::QueryReturnedNoRows {
                None
            } else {
                return Err(Error::from(err));
            }
        }
    };
    Ok(res)
}

fn delete_dentry_local(parent: u32, name: &str, tx: &Connection) -> Result<()> {
    if (!(name == ".." || name == ".")) {
        let sql = "DELETE FROM dentry WHERE parent_id=$1 and name=$2";
        tx.execute(sql, params![parent, name])?;
    } else {
        debug! {"Prevented the removal of {}", name};
    }
    Ok(())
}

fn delete_sub_dentry(id: u32, tx: &Connection) -> Result<()> {
    let sql = "DELETE FROM dentry WHERE parent_id=$1";
    tx.execute(sql, params![id])?;
    Ok(())
}

fn check_directory_is_empty_local(inode: u32, tx: &Connection) -> Result<bool> {
    let sql = "SELECT name FROM dentry where parent_id=$1";
    let mut stmt = tx.prepare(sql)?;
    let rows = stmt.query_map(params![inode], |row| {
        Ok({
            let name: String;
            name = row.get(0)?;
            name
        })
    })?;
    for row in rows {
        let name = row?;
        if &name != "." && &name != ".." {
            return Ok(false);
        }
    }
    Ok(true)
}

fn add_inode_local(attr: &DBFileAttr, tx: &Connection) -> Result<u32> {
    let sql = "INSERT INTO metadata \
            (size,\
            atime,\
            atime_nsec,\
            mtime,\
            mtime_nsec,\
            ctime,\
            ctime_nsec,\
            crtime,\
            crtime_nsec,\
            kind, \
            mode,\
            nlink,\
            uid,\
            gid,\
            rdev,\
            flags\
            ) \
            VALUES($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)";
    let atime = DateTime::<Utc>::from(attr.atime);
    let mtime = DateTime::<Utc>::from(attr.mtime);
    let ctime = DateTime::<Utc>::from(attr.ctime);
    let crtime = DateTime::<Utc>::from(attr.crtime);
    {
        tx.execute(
            sql,
            params![
                attr.size,
                atime.format("%Y-%m-%d %H:%M:%S").to_string(),
                atime.timestamp_subsec_nanos(),
                mtime.format("%Y-%m-%d %H:%M:%S").to_string(),
                mtime.timestamp_subsec_nanos(),
                ctime.format("%Y-%m-%d %H:%M:%S").to_string(),
                ctime.timestamp_subsec_nanos(),
                crtime.format("%Y-%m-%d %H:%M:%S").to_string(),
                crtime.timestamp_subsec_nanos(),
                file_type_to_const(attr.kind),
                attr.perm,
                0,
                attr.uid,
                attr.gid,
                attr.rdev,
                attr.flags,
            ],
        )?;
    }
    let sql = "SELECT last_insert_rowid()";
    let child: u32;
    {
        let mut stmt = tx.prepare(sql)?;
        child = stmt.query_row(params![], |row| row.get(0))?;
    }
    Ok(child)
}

pub struct Sqlite {
    conn: Connection,
    read_only: bool,
    time_recording: bool,
}

impl Sqlite {
    pub fn new(path: &Path, wal_mode: bool, syn_mode: &str) -> Result<Self> {
        let conn = Connection::open(path)?;
        let read_only = false;
        let time_recording = true;
        // enable foreign key. Sqlite ignores foreign key by default.
        conn.execute("PRAGMA foreign_keys=ON", [])?;
        conn.execute("PRAGMA cache_size=-16384", [])?;
        if (wal_mode) {
            conn.query_row("PRAGMA journal_mode=WAL", [], |_| Ok(true))?;
        }
        conn.execute(&("PRAGMA main.synchronous=".to_string() + (syn_mode)), [])?;
        Ok(Sqlite {
            conn,
            read_only,
            time_recording,
        })
    }
    pub fn new_no_time_recording(path: &Path, wal_mode: bool, syn_mode: &str) -> Result<Self> {
        let conn = Connection::open(path)?;
        let read_only = false;
        let time_recording = false;
        // enable foreign key. Sqlite ignores foreign key by default.
        conn.execute("PRAGMA foreign_keys=ON", [])?;
        conn.execute("PRAGMA cache_size=-16384", [])?;
        if (wal_mode) {
            conn.query_row("PRAGMA journal_mode=WAL", [], |_| Ok(true))?;
        }
        conn.execute(&("PRAGMA main.synchronous=".to_string() + (syn_mode)), [])?;
        Ok(Sqlite {
            conn,
            read_only,
            time_recording,
        })
    }
    pub fn new_at_time(path: &Path, time: String) -> Result<Self> {
        let conn = Connection::open(path)?;
        let read_only = true;
        let time_recording = true;
        // enable foreign key. Sqlite ignores foreign key by default.
        conn.execute("PRAGMA foreign_keys=ON", [])?;
        conn.execute("CREATE TEMP TABLE tdentry_audit_entries AS SELECT * FROM dentry_audit WHERE timestamp_utc < (?1);", params![time])?;
        conn.execute("CREATE TEMP TABLE tmetadata_audit_entries AS SELECT * FROM metadata_audit WHERE timestamp_utc < (?1);", params![time])?;
        conn.execute("CREATE TEMP TABLE tdata_audit_entries AS SELECT * FROM data_audit WHERE timestamp_utc < (?1);", params![time])?;
        conn.execute("CREATE TEMP TABLE txattr_audit_entries AS SELECT * FROM xattr_audit WHERE timestamp_utc < (?1);", params![time] )?;
        //STRATEGY: (SELECT  max(timestamp) utc, CK/PK FROM table GROUP BY CK/PK) as latest, join on max_ts, CK/PK with table's.
        //conn.execute("CREATE TEMP TABLE txattr_audit_entries AS SELECT * FROM xattr_audit WHERE timestamp_utc < (?1);", params![time] )?;
        conn.execute("CREATE TEMP TABLE tdentry AS SELECT * FROM (SELECT max_ts, latest.parent_id, latest.child_id, TG_OP, latest.name, tdentry_audit_entries.file_type from (SELECT max(timestamp_utc) as max_ts, parent_id, child_id, name FROM tdentry_audit_entries as latest GROUP BY parent_id, child_id, name) as latest INNER JOIN tdentry_audit_entries ON tdentry_audit_entries.timestamp_utc=max_ts AND tdentry_audit_entries.child_id=latest.child_id AND tdentry_audit_entries.name = latest.name AND tdentry_audit_entries.parent_id=latest.parent_id) WHERE TG_OP IS NOT 'DELETE';", [])?;
        conn.execute("CREATE TEMP TABLE tdata AS SELECT * FROM (SELECT max_ts, latest.block_num, latest.file_id, tdata_audit_entries.data, TG_OP from (SELECT max(timestamp_utc) as max_ts, file_id, block_num FROM tdata_audit_entries as latest GROUP BY file_id, block_num) as latest INNER JOIN tdata_audit_entries ON tdata_audit_entries.timestamp_utc=max_ts AND tdata_audit_entries.file_id=latest.file_id AND tdata_audit_entries.block_num = latest.block_num) WHERE TG_OP IS NOT 'DELETE';", [])?;
        conn.execute("CREATE TEMP TABLE txattr AS SELECT * FROM (SELECT max_ts, latest.name, latest.file_id, txattr_audit_entries.name, txattr_audit_entries.value, TG_OP from (SELECT max(timestamp_utc) as max_ts, file_id, name FROM txattr_audit_entries as latest GROUP BY file_id, name) as latest INNER JOIN txattr_audit_entries ON txattr_audit_entries.timestamp_utc=max_ts AND txattr_audit_entries.file_id=latest.file_id AND txattr_audit_entries.name = latest.name) WHERE TG_OP IS NOT 'DELETE';", [])?;
        conn.execute("CREATE TEMP TABLE tmetadata AS SELECT * FROM (SELECT max_ts, tmetadata_audit_entries.id, size, atime, atime_nsec, mtime, mtime_nsec, ctime, ctime_nsec, crtime, crtime_nsec, kind, mode, nlink, uid, gid, rdev, flags, TG_OP from (SELECT max(timestamp_utc) as max_ts, id FROM tmetadata_audit_entries as latest GROUP BY id) as latest INNER JOIN tmetadata_audit_entries ON tmetadata_audit_entries.timestamp_utc=max_ts AND tmetadata_audit_entries.id=latest.id) WHERE TG_OP IS NOT 'DELETE';", [])?;
        Ok(Sqlite {
            conn,
            read_only,
            time_recording,
        })
    }
    pub fn new_read_only(path: &Path) -> Result<Self> {
        let conn = Connection::open(path)?;
        let read_only = true;
        let time_recording = false;
        // enable foreign key. Sqlite ignores foreign key by default.
        conn.execute("PRAGMA foreign_keys=ON", [])?;
        Ok(Sqlite {
            conn,
            read_only,
            time_recording,
        })
    }
    pub fn new_in_memory() -> Result<Self> {
        let conn = Connection::open_in_memory()?;
        let read_only = false;
        let time_recording = true;
        // enable foreign key. Sqlite ignores foreign key by default.
        conn.execute("PRAGMA foreign_keys=ON", [])?;
        Ok(Sqlite {
            conn,
            read_only,
            time_recording,
        })
    }
}

impl DbModule for Sqlite {
    fn init(&mut self) -> Result<()> {
        let table_search_sql =
            "SELECT count(name) FROM sqlite_master WHERE type='table' AND name=$1";
        {
            let row_count: u32 =
                self.conn
                    .query_row(table_search_sql, params!["metadata"], |row| row.get(0))?;
            if row_count == 0 {
                let sql = "CREATE TABLE metadata(\
                    id integer primary key AUTOINCREMENT,\
                    size int default 0 not null,\
                    atime text,\
                    atime_nsec int,\
                    mtime text,\
                    mtime_nsec int,\
                    ctime text,\
                    ctime_nsec int,\
                    crtime text,\
                    crtime_nsec int,\
                    kind int,\
                    mode int,\
                    nlink int default 0 not null,\
                    uid int default 0,\
                    gid int default 0,\
                    rdev int default 0,\
                    flags int default 0 \
                    )";
                let res = self.conn.execute(sql, params![])?;
                debug!("metadata table: {}", res);
                let sql_audit_table = "CREATE TABLE metadata_audit(\
                    seq integer PRIMARY KEY AUTOINCREMENT,\
                    timestamp_utc text,\
                    TG_OP text,\
                    id integer,\
                    size int default 0,\
                    atime text,\
                    atime_nsec int,\
                    mtime text,\
                    mtime_nsec int,\
                    ctime text,\
                    ctime_nsec int,\
                    crtime text,\
                    crtime_nsec int,\
                    kind int,\
                    mode int,\
                    nlink int default 0,\
                    uid int default 0,\
                    gid int default 0,\
                    rdev int default 0,\
                    flags int default 0 \
                    )";
                let res_audit_table = self.conn.execute(sql_audit_table, params![])?;
                debug!("metadata table: {}", res_audit_table);
                let sql_audit_trigger_delete = "\
                CREATE TRIGGER audit_delete_metadata AFTER DELETE on metadata \
                BEGIN \
                    INSERT INTO metadata_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'DELETE', \
                    OLD.id, OLD.size, OLD.atime, OLD.atime_nsec, \
                    OLD.mtime, OLD.mtime_nsec, OLD.ctime, OLD.ctime_nsec, \
                    OLD.crtime, OLD.crtime_nsec, \
                    OLD.kind, OLD.mode, OLD.nlink, \
                    OLD.uid, OLD.gid, OLD.rdev, OLD.flags); \
                END \
                ";
                let res_audit_trigger_delete =
                    self.conn.execute(sql_audit_trigger_delete, params![])?;
                debug!("metadata table: {}", res_audit_trigger_delete);
                let sql_audit_trigger_update = "\
                CREATE TRIGGER audit_update_metadata AFTER UPDATE on metadata \
                BEGIN \
                    INSERT INTO metadata_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'UPDATE', \
                    NEW.id, NEW.size, NEW.atime, NEW.atime_nsec, \
                    NEW.mtime, NEW.mtime_nsec, NEW.ctime, NEW.ctime_nsec, \
                    NEW.crtime, NEW.crtime_nsec, \
                    NEW.kind, NEW.mode, NEW.nlink, \
                    NEW.uid, NEW.gid, NEW.rdev, NEW.flags); \
                END \
                ";
                let res_audit_trigger_update =
                    self.conn.execute(sql_audit_trigger_update, params![])?;
                debug!("metadata table: {}", res_audit_trigger_update);
                let sql_audit_trigger_insert = "\
                CREATE TRIGGER audit_insert_metadata AFTER INSERT on metadata \
                BEGIN \
                    INSERT INTO metadata_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'INSERT', \
                    NEW.id, NEW.size, NEW.atime, NEW.atime_nsec, \
                    NEW.mtime, NEW.mtime_nsec, NEW.ctime, NEW.ctime_nsec, \
                    NEW.crtime, NEW.crtime_nsec, \
                    NEW.kind, NEW.mode, NEW.nlink, \
                    NEW.uid, NEW.gid, NEW.rdev, NEW.flags); \
                END \
                ";
                let res_audit_trigger_insert =
                    self.conn.execute(sql_audit_trigger_insert, params![])?;
                debug!("metadata table: {}", res_audit_trigger_insert);
            }
        }
        {
            let row_count: u32 =
                self.conn
                    .query_row(table_search_sql, params!["dentry"], |row| row.get(0))?;
            if row_count == 0 {
                let sql = "CREATE TABLE dentry(\
                    parent_id int,\
                    child_id int,\
                    file_type int,\
                    name text,\
                    foreign key (parent_id) references metadata(id) on delete cascade,\
                    foreign key (child_id) references metadata(id) on delete cascade,\
                    primary key (parent_id, name) \
                    )";
                self.conn.execute(sql, params![])?;
                let sql_audit_table = "CREATE TABLE dentry_audit(\
                    seq integer PRIMARY KEY AUTOINCREMENT,\
                    timestamp_utc text,\
                    TG_OP text,\
                    parent_id int,\
                    child_id int,\
                    file_type int,\
                    name text\
                    )";
                self.conn.execute(sql_audit_table, params![])?;
                let sql_audit_trigger_delete = "\
                CREATE TRIGGER audit_delete_dentry AFTER DELETE on dentry \
                BEGIN \
                    INSERT INTO dentry_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'DELETE', \
                    OLD.parent_id, OLD.child_id, OLD.file_type, \
                    OLD.name); \
                END \
                ";
                let res_audit_trigger_delete =
                    self.conn.execute(sql_audit_trigger_delete, params![])?;
                debug!("dentry table: {}", res_audit_trigger_delete);
                let sql_audit_trigger_update = "\
                CREATE TRIGGER audit_update_dentry AFTER UPDATE on dentry \
                BEGIN \
                    INSERT INTO dentry_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'UPDATE', \
                    NEW.parent_id, NEW.child_id, NEW.file_type, \
                    NEW.name); \
                END";
                let res_audit_trigger_update =
                    self.conn.execute(sql_audit_trigger_update, params![])?;
                debug!("dentry table: {}", res_audit_trigger_update);
                let sql_audit_trigger_insert = "\
                CREATE TRIGGER audit_insert_dentry AFTER INSERT on dentry \
                BEGIN \
                    INSERT INTO dentry_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'INSERT', \
                    NEW.parent_id, NEW.child_id, NEW.file_type, \
                    NEW.name); \
                END";
                let res_audit_trigger_insert =
                    self.conn.execute(sql_audit_trigger_insert, params![])?;
                debug!("dentry table: {}", res_audit_trigger_insert);
            }
        }
        {
            let row_count: u32 = self
                .conn
                .query_row(table_search_sql, params!["data"], |row| row.get(0))?;
            if row_count == 0 {
                let sql = "CREATE TABLE data(\
                    file_id int,\
                    block_num int,\
                    data blob,\
                    foreign key (file_id) references metadata(id) on delete cascade,\
                    primary key (file_id, block_num) \
                    )";
                self.conn.execute(sql, params![])?;
                let sql_audit_table = "CREATE TABLE data_audit(\
                    seq integer PRIMARY KEY AUTOINCREMENT,\
                    timestamp_utc text,\
                    TG_OP text,\
                    file_id int,\
                    block_num int,\
                    data blob\
                    )";
                self.conn.execute(sql_audit_table, params![])?;
                let sql_audit_trigger_delete = "\
                CREATE TRIGGER audit_delete_data AFTER DELETE on data \
                BEGIN \
                    INSERT INTO data_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'DELETE', \
                    OLD.file_id, OLD.block_num, OLD.data \
                    ); \
                END \
                ";
                let res_audit_trigger_delete =
                    self.conn.execute(sql_audit_trigger_delete, params![])?;
                debug!("data table: {}", res_audit_trigger_delete);
                let sql_audit_trigger_update = "\
                CREATE TRIGGER audit_update_data AFTER UPDATE on data \
                BEGIN \
                    INSERT INTO data_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'UPDATE', \
                    NEW.file_id, NEW.block_num, NEW.data \
                    ); \
                END";
                let res_audit_trigger_update =
                    self.conn.execute(sql_audit_trigger_update, params![])?;
                debug!("data table: {}", res_audit_trigger_update);
                let sql_audit_trigger_insert = "\
                CREATE TRIGGER audit_insert_data AFTER INSERT on data \
                BEGIN \
                    INSERT INTO data_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'INSERT', \
                    NEW.file_id, NEW.block_num, NEW.data \
                    ); \
                END";
                let res_audit_trigger_insert =
                    self.conn.execute(sql_audit_trigger_insert, params![])?;
                debug!("data table: {}", res_audit_trigger_insert);
            }
        }
        {
            let row_count: u32 =
                self.conn
                    .query_row(table_search_sql, params!["xattr"], |row| row.get(0))?;
            if row_count == 0 {
                let sql = "CREATE TABLE xattr(\
                    file_id int,\
                    name text,\
                    value text,\
                    foreign key (file_id) references metadata(id) on delete cascade,\
                    primary key (file_id, name) \
                    )";
                self.conn.execute(sql, params![])?;
                let sql_audit_table = "CREATE TABLE xattr_audit(\
                    seq integer PRIMARY KEY AUTOINCREMENT,\
                    timestamp_utc text,\
                    TG_OP text,\
                    file_id int,\
                    name text,\
                    value text\
                    )";
                self.conn.execute(sql_audit_table, params![])?;
                let sql_audit_trigger_delete = "\
                CREATE TRIGGER audit_delete_xattr AFTER DELETE on xattr \
                BEGIN \
                    INSERT INTO xattr_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'DELETE', \
                    OLD.file_id, OLD.name, OLD.value \
                    );\
                END \
                ";
                let res_audit_trigger_delete =
                    self.conn.execute(sql_audit_trigger_delete, params![])?;
                debug!("xattr table: {}", res_audit_trigger_delete);
                let sql_audit_trigger_update = "\
                CREATE TRIGGER audit_update_xattr AFTER UPDATE on xattr FOR EACH ROW \
                BEGIN \
                    INSERT INTO xattr_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'UPDATE', \
                    NEW.file_id, NEW.name, NEW.value \
                    ); \
                END";
                let res_audit_trigger_update =
                    self.conn.execute(sql_audit_trigger_update, params![])?;
                debug!("xattr table: {}", res_audit_trigger_update);
                let sql_audit_trigger_insert = "\
                CREATE TRIGGER audit_insert_xattr AFTER INSERT on xattr FOR EACH ROW \
                BEGIN \
                    INSERT INTO xattr_audit VALUES (NULL, strftime('%Y-%m-%dT%H:%M:%f', 'now', 'utc'), 'INSERT', \
                    NEW.file_id, NEW.name, NEW.value \
                    ); \
                END";
                let res_audit_trigger_insert =
                    self.conn.execute(sql_audit_trigger_insert, params![])?;
                debug!("xattr table: {}", res_audit_trigger_insert);
            }
        }
        {
            let sql = "SELECT count(id) FROM metadata WHERE id=1";
            let row_count: u32 = self.conn.query_row(sql, params![], |row| row.get(0))?;
            if row_count == 0 {
                let now = SystemTime::now();
                let root_dir = DBFileAttr {
                    ino: 1,
                    size: 0,
                    blocks: 0,
                    atime: now,
                    mtime: now,
                    ctime: now,
                    crtime: now,
                    kind: FileType::Directory,
                    perm: 0o40777,
                    nlink: 0,
                    uid: 0,
                    gid: 0,
                    rdev: 0,
                    flags: 0,
                };
                add_inode_local(&root_dir, &self.conn)?;
            }
        }
        {
            let sql = "SELECT count(parent_id) FROM dentry WHERE parent_id=1 and name='.'";
            let row_count: u32 = self.conn.query_row(sql, params![], |row| row.get(0))?;
            if row_count == 0 {
                let root_dir = DEntry {
                    parent_ino: 1,
                    child_ino: 1,
                    file_type: FileType::Directory,
                    filename: ".".to_string(),
                };
                add_dentry(root_dir, &self.conn)?;
            }
        }
        {
            let sql = "SELECT count(parent_id) FROM dentry WHERE parent_id=1 and name='..'";
            let row_count: u32 = self.conn.query_row(sql, params![], |row| row.get(0))?;
            if row_count == 0 {
                let root_dir = DEntry {
                    parent_ino: 1,
                    child_ino: 1,
                    file_type: FileType::Directory,
                    filename: "..".to_string(),
                };
                add_dentry(root_dir, &self.conn)?;
            }
        }
        Ok(())
    }

    fn get_inode(&self, inode: u32) -> Result<Option<DBFileAttr>> {
        get_inode_local(inode, &self.conn)
    }
    fn get_inode_at_time(&self, inode: u32, time: String) -> Result<Option<DBFileAttr>> {
        debug! {"get_inode_at_time: {}, {}", inode, time};
        let r = get_inode_local_at_time(inode, &self.conn, time);
        match r {
            Err(ref x) => {
                debug! {"ERROR: {}", format!("{}", x)};
                return r;
            }
            Ok(_) => return r,
        }
    }

    fn add_inode_and_dentry(&mut self, parent: u32, name: &str, attr: &DBFileAttr) -> Result<u32> {
        let tx = self.conn.transaction()?;
        let child = add_inode_local(attr, &tx)?;
        let dentry = DEntry {
            parent_ino: parent,
            child_ino: child,
            filename: String::from(name),
            file_type: attr.kind,
        };
        add_dentry(dentry, &tx)?;
        if attr.kind == FileType::Directory {
            let dentry = DEntry {
                parent_ino: child,
                child_ino: parent,
                filename: String::from(".."),
                file_type: attr.kind,
            };
            add_dentry(dentry, &tx)?;
            let dentry = DEntry {
                parent_ino: child,
                child_ino: child,
                filename: String::from("."),
                file_type: attr.kind,
            };
            add_dentry(dentry, &tx)?;
        }
        let now = Utc::now();
        if (self.time_recording) {
            update_mtime(parent, now, &tx)?;
            update_ctime(parent, now, &tx)?;
        }
        tx.commit()?;
        Ok(child)
    }

    fn update_inode(&mut self, attr: &DBFileAttr, truncate: bool) -> Result<()> {
        let sql = "UPDATE metadata SET \
            size=$1,\
            atime=datetime($2),\
            atime_nsec=$3,\
            mtime=datetime($4),\
            mtime_nsec=$5,\
            ctime=datetime($6),\
            ctime_nsec=$7,\
            crtime=datetime($8),\
            crtime_nsec=$9,\
            mode=$10,\
            uid=$11,\
            gid=$12,\
            rdev=$13,\
            flags=$14 \
             WHERE id=$15";
        let tx = self.conn.transaction()?;
        let oldattr = get_inode_local(attr.ino, &tx)?;
        let oldattr = match oldattr {
            Some(n) => n,
            None => {
                return Err(Error::from(ErrorKind::FsNoEnt {
                    description: format!("{} is not exist", attr.ino),
                }));
            }
        };
        let now = Utc::now();
        let atime = DateTime::<Utc>::from(attr.atime);
        let mtime = if oldattr.size != attr.size {
            now
        } else {
            DateTime::<Utc>::from(attr.mtime)
        };
        let ctime = now;
        let crtime = DateTime::<Utc>::from(attr.crtime);
        {
            let mut stmt = tx.prepare(sql)?;
            stmt.execute(params![
                attr.size,
                atime.format("%Y-%m-%d %H:%M:%S").to_string(),
                atime.timestamp_subsec_nanos(),
                mtime.format("%Y-%m-%d %H:%M:%S").to_string(),
                mtime.timestamp_subsec_nanos(),
                ctime.format("%Y-%m-%d %H:%M:%S").to_string(),
                ctime.timestamp_subsec_nanos(),
                crtime.format("%Y-%m-%d %H:%M:%S").to_string(),
                crtime.timestamp_subsec_nanos(),
                attr.perm,
                attr.uid,
                attr.gid,
                attr.rdev,
                attr.flags,
                attr.ino
            ])?;
        }
        if truncate {
            release_data(attr.ino, attr.size, &tx)?;
        }
        tx.commit()?;
        Ok(())
    }

    fn delete_inode_if_noref(&mut self, inode: u32) -> Result<()> {
        let sql = "SELECT count(child_id) FROM dentry WHERE child_id=$1";
        let tx = self.conn.transaction()?;
        let nlink: u32;
        {
            let mut stmt = tx.prepare(sql)?;
            nlink = stmt.query_row(params![inode], |row| row.get(0))?;
        }
        if nlink == 0 {
            let sql = "DELETE FROM metadata WHERE id=$1";
            tx.execute(sql, params![inode])?;
        }
        tx.commit()?;
        Ok(())
    }

    fn get_dentry(&self, inode: u32) -> Result<Vec<DEntry>> {
        let sql = "SELECT child_id, file_type, name FROM dentry WHERE parent_id=$1 ORDER BY name";
        let mut stmt = self.conn.prepare(sql)?;
        let rows = stmt.query_map(params![inode], |row| {
            Ok(DEntry {
                parent_ino: inode,
                child_ino: row.get(0)?,
                file_type: const_to_file_type(row.get(1)?),
                filename: row.get(2)?,
            })
        })?;
        let mut entries: Vec<DEntry> = Vec::new();
        for row in rows {
            entries.push(row?);
        }
        Ok(entries)
    }
    fn get_dentry_at_time(&self, inode: u32, _time: String) -> Result<Vec<DEntry>> {
        let sql = "SELECT child_id, file_type, name FROM tdentry WHERE parent_id=$1 ORDER BY name";
        let mut stmt = self.conn.prepare(sql)?;
        let rows = stmt.query_map(params![inode], |row| {
            Ok(DEntry {
                parent_ino: inode,
                child_ino: row.get(0)?,
                file_type: const_to_file_type(row.get(1)?),
                filename: row.get(2)?,
            })
        })?;
        let mut entries: Vec<DEntry> = Vec::new();
        for row in rows {
            entries.push(row?);
        }
        Ok(entries)
    }

    fn link_dentry(&mut self, inode: u32, parent: u32, name: &str) -> Result<DBFileAttr> {
        let now = Utc::now();
        let tx = self.conn.transaction()?;
        let attr = match get_inode_local(inode, &tx)? {
            Some(n) => n,
            None => {
                return Err(Error::from(ErrorKind::FsNoEnt {
                    description: format!("old path {} is not exist", inode),
                }));
            }
        };
        if attr.kind != FileType::RegularFile {
            return Err(Error::from(ErrorKind::FsParm {
                description: format!("old path {} is not a regular file", inode),
            }));
        };
        let new_inode = get_dentry_single(parent, name, &tx)?;
        if new_inode.is_some() {
            return Err(Error::from(ErrorKind::FsFileExist {
                description: format!("new path {}/{} exist", parent, name),
            }));
        }
        let entry = DEntry {
            parent_ino: parent,
            child_ino: inode,
            file_type: FileType::RegularFile,
            filename: name.to_string(),
        };
        add_dentry(entry, &tx)?;
        if (self.time_recording) {
            update_mtime(inode, now, &tx)?;
            update_mtime(parent, now, &tx)?;
            update_ctime(parent, now, &tx)?;
        }
        tx.commit()?;
        Ok(attr)
    }

    fn delete_dentry(&mut self, parent: u32, name: &str) -> Result<u32> {
        let sql = "SELECT child_id FROM dentry WHERE parent_id=$1 and name=$2";
        let now = Utc::now();
        let tx = self.conn.transaction()?;
        let child: u32;
        {
            let mut stmt = tx.prepare(sql)?;
            child = stmt.query_row(params![parent, name], |row| row.get(0))?;
        }
        delete_dentry_local(parent, name, &tx)?;
        delete_sub_dentry(child, &tx)?;
        if (self.time_recording) {
            update_ctime(child, now, &tx)?;
            update_mtime(parent, now, &tx)?;
            update_ctime(parent, now, &tx)?;
        }
        tx.commit()?;
        Ok(child)
    }

    fn move_dentry(
        &mut self,
        parent: u32,
        name: &str,
        new_parent: u32,
        new_name: &str,
    ) -> Result<Option<u32>> {
        let sql = "UPDATE dentry SET parent_id=$1, name=$2 where parent_id=$3 and name=$4";
        let now = Utc::now();
        let tx = self.conn.transaction()?;
        let dentry = match get_dentry_single(parent, name, &tx)? {
            Some(n) => n,
            None => {
                return Err(Error::from(ErrorKind::FsNoEnt {
                    description: format!("parent: {} name:{}", parent, name),
                }));
            }
        };
        let mut res = None;
        let exist_entry = get_dentry_single(new_parent, new_name, &tx)?;
        if let Some(v) = exist_entry {
            let exist_id = v.child_ino;
            let exist_file_type = v.file_type;
            if dentry.file_type != exist_file_type {
                match exist_file_type {
                    FileType::Directory => {
                        return Err(Error::from(ErrorKind::FsIsDir {
                            description: format!("parent: {} name:{}", new_parent, new_name),
                        }));
                    }
                    FileType::RegularFile => {
                        return Err(Error::from(ErrorKind::FsIsNotDir {
                            description: format!("parent: {} name:{}", new_parent, new_name),
                        }));
                    }
                    _ => {
                        return Err(Error::from(ErrorKind::Undefined {
                            description: format!(
                                "parent: {} name:{} has invalid type: {:?}",
                                new_parent, new_name, exist_file_type
                            ),
                        }));
                    }
                };
            }
            if exist_file_type == FileType::Directory {
                let empty = check_directory_is_empty_local(exist_id, &tx)?;
                if !empty {
                    return Err(Error::from(ErrorKind::FsNotEmpty {
                        description: format!(
                            "parent: {} name:{} is not empty",
                            new_parent, new_name
                        ),
                    }));
                }
            }
            delete_dentry_local(new_parent, new_name, &tx)?;
            res = Some(v.child_ino);
        }
        tx.execute(sql, params![new_parent, new_name, parent, name])?;
        if parent != new_parent && dentry.file_type == FileType::Directory {
            let sql = "UPDATE dentry set child_id=$1 WHERE parent_id=$2 and name='..'";
            tx.execute(sql, params![new_parent, dentry.child_ino])?;
        }
        if (self.time_recording) {
            update_ctime(dentry.child_ino, now, &tx)?;
            update_mtime(parent, now, &tx)?;
            update_ctime(parent, now, &tx)?;
            if parent != new_parent {
                update_mtime(new_parent, now, &tx)?;
                update_ctime(new_parent, now, &tx)?;
            }
        }
        tx.commit()?;
        Ok(res)
    }

    fn check_directory_is_empty(&self, inode: u32) -> Result<bool> {
        check_directory_is_empty_local(inode, &self.conn)
    }

    fn lookup(&mut self, parent: u32, name: &str) -> Result<Option<DBFileAttr>> {
        let sql = "SELECT \
            metadata.id,\
            metadata.size,\
            metadata.atime,\
            metadata.atime_nsec,\
            metadata.mtime,\
            metadata.mtime_nsec,\
            metadata.ctime,\
            metadata.ctime_nsec,\
            metadata.crtime,\
            metadata.crtime_nsec,\
            metadata.kind, \
            metadata.mode,\
            ncount.nlink,\
            metadata.uid,\
            metadata.gid,\
            metadata.rdev,\
            metadata.flags, \
            blocknum.block_num \
            FROM dentry \
            INNER JOIN metadata \
            ON metadata.id=dentry.child_id \
            AND dentry.parent_id=$1 \
            AND dentry.name=$2 \
            LEFT JOIN (SELECT file_id file_id, count(block_num) block_num from data) AS blocknum \
            ON dentry.child_id = blocknum.file_id \
            LEFT JOIN ( SELECT child_id, COUNT(child_id) nlink FROM dentry GROUP BY child_id) AS ncount \
            ON dentry.child_id = ncount.child_id \
            ";
        let tx = self.conn.transaction()?;
        let stmt = tx.prepare(sql)?;
        let params = params![parent, name];
        let result = parse_attr(stmt, params);
        if (self.time_recording) {
            update_atime(parent, Utc::now(), &tx)?;
        }
        tx.commit()?;
        result
    }
    fn lookup_at_time(
        &mut self,
        parent: u32,
        name: &str,
        _time: String,
    ) -> Result<Option<DBFileAttr>> {
        let sql = "SELECT \
            tmetadata.id,\
            tmetadata.size,\
            tmetadata.atime,\
            tmetadata.atime_nsec,\
            tmetadata.mtime,\
            tmetadata.mtime_nsec,\
            tmetadata.ctime,\
            tmetadata.ctime_nsec,\
            tmetadata.crtime,\
            tmetadata.crtime_nsec,\
            tmetadata.kind, \
            tmetadata.mode,\
            ncount.nlink,\
            tmetadata.uid,\
            tmetadata.gid,\
            tmetadata.rdev,\
            tmetadata.flags, \
            blocknum.block_num \
            FROM tdentry \
            INNER JOIN tmetadata \
            ON tmetadata.id=tdentry.child_id \
            AND tdentry.parent_id=$1 \
            AND tdentry.name=$2 \
            LEFT JOIN (SELECT file_id file_id, count(block_num) block_num from tdata) AS blocknum \
            ON tdentry.child_id = blocknum.file_id \
            LEFT JOIN ( SELECT child_id, COUNT(child_id) nlink FROM tdentry GROUP BY child_id) AS ncount \
            ON tdentry.child_id = ncount.child_id \
            ";
        let tx = self.conn.transaction()?;
        let stmt = tx.prepare(sql)?;
        let params = params![parent, name];
        let result = parse_attr(stmt, params);
        //update_atime(parent, Utc::now(), &tx)?;
        tx.commit()?;
        result
    }

    fn get_data(&mut self, inode: u32, block: u32, length: u32) -> Result<Vec<u8>> {
        let tx = self.conn.transaction()?;
        let row: Vec<u8>;
        {
            let mut stmt = tx.prepare(
                "SELECT \
                data FROM data WHERE file_id=$1 AND block_num=$2",
            )?;
            row = match stmt.query_row(params![inode, block], |row| row.get(0)) {
                Ok(n) => n,
                Err(err) => {
                    if err == rusqlite::Error::QueryReturnedNoRows {
                        vec![0; length as usize]
                    } else {
                        return Err(Error::from(err));
                    }
                }
            };
        }
        if ((!(self.read_only)) && self.time_recording) {
            update_atime(inode, Utc::now(), &tx)?;
        }
        tx.commit()?;
        Ok(row)
    }
    fn get_data_at_time(
        &mut self,
        inode: u32,
        block: u32,
        length: u32,
        _time: String,
    ) -> Result<Vec<u8>> {
        let tx = self.conn.transaction()?;
        let row: Vec<u8>;
        {
            let mut stmt = tx.prepare(
                "SELECT \
                data FROM tdata WHERE file_id=$1 AND block_num=$2",
            )?;
            row = match stmt.query_row(params![inode, block], |row| row.get(0)) {
                Ok(n) => n,
                Err(err) => {
                    if err == rusqlite::Error::QueryReturnedNoRows {
                        vec![0; length as usize]
                    } else {
                        return Err(Error::from(err));
                    }
                }
            };
        }
        if (self.time_recording) {
            update_atime(inode, Utc::now(), &tx)?;
        }
        tx.commit()?;
        Ok(row)
    }

    fn write_data(&mut self, inode: u32, block: u32, data: &[u8], size: u32) -> Result<()> {
        let tx = self.conn.transaction()?;
        {
            let db_size: u32 = tx.query_row(
                "SELECT size FROM metadata WHERE id=$1",
                params![inode],
                |row| row.get(0),
            )?;
            tx.execute(
                "REPLACE INTO data \
            (file_id, block_num, data)
            VALUES($1, $2, $3)",
                params![inode, block, data],
            )?;
            if size > db_size {
                tx.execute(
                    "UPDATE metadata SET size=$1 WHERE id=$2",
                    params![size, inode],
                )?;
            }
        }
        let time = Utc::now();
        if (self.time_recording) {
            update_mtime(inode, time, &tx)?;
            update_ctime(inode, time, &tx)?;
        }
        tx.commit()?;
        Ok(())
    }

    fn release_data(&self, inode: u32) -> Result<()> {
        self.conn
            .execute("DELETE FROM data WHERE file_id=$1", params![inode])?;
        Ok(())
    }

    fn delete_all_noref_inode(&mut self) -> Result<()> {
        self.conn.execute(
            "DELETE FROM metadata WHERE NOT EXISTS (SELECT 'x' FROM dentry WHERE metadata.id = dentry.child_id)",
            params![]
        )?;
        Ok(())
    }

    fn get_db_block_size(&self) -> u32 {
        BLOCK_SIZE
    }
    fn get_db_block_size_at_time(&self, _time: String) -> u32 {
        BLOCK_SIZE
    }

    fn set_xattr(&mut self, inode: u32, key: &str, value: &[u8]) -> Result<()> {
        let tx = self.conn.transaction()?;
        {
            tx.execute(
                "REPLACE INTO xattr \
            (file_id, name, value)
            VALUES($1, $2, $3)",
                params![inode, key, value],
            )?;
        }
        let time = Utc::now();
        if (self.time_recording) {
            update_ctime(inode, time, &tx)?;
        }
        tx.commit()?;
        Ok(())
    }

    fn get_xattr(&self, inode: u32, key: &str) -> Result<Vec<u8>> {
        let mut stmt = self.conn.prepare(
            "SELECT \
            value FROM xattr WHERE file_id=$1 AND name=$2",
        )?;
        let row: Vec<u8> = match stmt.query_row(params![inode, key], |row| row.get(0)) {
            Ok(n) => n,
            Err(err) => {
                if err == rusqlite::Error::QueryReturnedNoRows {
                    return Err(Error::from(ErrorKind::FsNoEnt {
                        description: format!("inode: {} name:{}", inode, key),
                    }));
                } else {
                    return Err(Error::from(err));
                }
            }
        };
        Ok(row)
    }
    fn get_xattr_at_time(&self, inode: u32, key: &str, _time: String) -> Result<Vec<u8>> {
        let mut stmt = self.conn.prepare(
            "SELECT \
            value FROM txattr WHERE file_id=$1 AND name=$2",
        )?;
        let row: Vec<u8> = match stmt.query_row(params![inode, key], |row| row.get(0)) {
            Ok(n) => n,
            Err(err) => {
                if err == rusqlite::Error::QueryReturnedNoRows {
                    return Err(Error::from(ErrorKind::FsNoEnt {
                        description: format!("inode: {} name:{}", inode, key),
                    }));
                } else {
                    return Err(Error::from(err));
                }
            }
        };
        Ok(row)
    }

    fn list_xattr(&self, inode: u32) -> Result<Vec<String>> {
        let sql = "SELECT name FROM xattr WHERE file_id=$1 ORDER BY name";
        let mut stmt = self.conn.prepare(sql)?;
        let rows = stmt.query_map(params![inode], |row| Ok(row.get(0)?))?;
        let mut name_list: Vec<String> = Vec::new();
        for row in rows {
            name_list.push(row?);
        }
        Ok(name_list)
    }
    fn list_xattr_at_time(&self, inode: u32, _time: String) -> Result<Vec<String>> {
        let sql = "SELECT name FROM txattr WHERE file_id=$1 ORDER BY name";
        let mut stmt = self.conn.prepare(sql)?;
        let rows = stmt.query_map(params![inode], |row| Ok(row.get(0)?))?;
        let mut name_list: Vec<String> = Vec::new();
        for row in rows {
            name_list.push(row?);
        }
        Ok(name_list)
    }

    fn delete_xattr(&mut self, inode: u32, key: &str) -> Result<()> {
        let tx = self.conn.transaction()?;
        {
            tx.execute(
                "DELETE FROM xattr \
            WHERE file_id = $1 AND name = $2",
                params![inode, key],
            )?;
        }
        let time = Utc::now();
        if (self.time_recording) {
            update_ctime(inode, time, &tx)?;
        }
        tx.commit()?;
        Ok(())
    }
}
