#[macro_use] extern crate failure;
#[macro_use] extern crate log;
#[macro_use] extern crate clap;
use std::env;
use std::ffi::OsStr;
use sqlite_fs::filesystem::SqliteFs;
use clap::{App, Arg};
use sqlite_fs::db_module::sqlite::Sqlite;
use sqlite_fs::db_module::DbModule;

fn main() {
    env_logger::init();

    let mount_option_arg = Arg::with_name("mount_option")
        .short("o")
        .long("option")
        .help("Additional mount option for this filesystem")
        .takes_value(true)
        .multiple(true);

    let mount_point_arg = Arg::with_name("mount_point")
        .help("Target mountpoint path")
        .index(1)
        .required(true);

    let db_path_arg = Arg::with_name("db_path")
        .help("Sqlite database file path. If not set, open database in memory. Rewind is not supported if an in-memory database is used.")
        .index(2);

    let db_time_arg = Arg::with_name("at_time")
        .short("t")
        .long("time")
        .help("Sqlite database time to rewind to. If specified, implies read-only.")
        .takes_value(true);

    let db_read_only_arg = Arg::with_name("read_only")
        .short("r")
        .long("read-only")
        .help("Mount as read-only.");

    let matches = App::new("sqlitefs")
        .about("Sqlite database as a filesystem.")
        .version(crate_version!())
        .arg(mount_option_arg)
        .arg(mount_point_arg)
        .arg(db_path_arg)
        .arg(db_time_arg)
        .arg(db_read_only_arg)
        .get_matches();

    let mut option_vals = ["-o", "fsname=sqlitefs", "-o", "default_permissions", "-o", "allow_other", "-o", "read_only"].to_vec();
    if let Some(v) = matches.values_of("mount_option") {
        for i in v {
            option_vals.push("-o");
            option_vals.push(i);
        }
    }

    let mountpoint = matches.value_of("mount_point").expect("Mount point path is missing.");
    let db_path = matches.value_of("db_path");
    let db_time = matches.value_of("at_time");
    let db_read_only: bool = matches.is_present("read_only");
    let options = option_vals
        .iter()
        .map(|o| o.as_ref())
        .collect::<Vec<&OsStr>>();
    let fs: SqliteFs;
    debug!("read-only: {}", db_read_only);
    match db_path {
        Some(path) => {
            match db_time {
                Some(time) => {
                    debug!("Requested rewind at: {:?}", time);
                    fs = match SqliteFs::new_at_time(path, time.to_string()) {
                        Ok(n) => n,
                        Err(err) => {println!("{:?}", err); return;}
                    };
                }
                None => {
                    debug!("Rewind is not requested; proceeding as-is.");
                    if(db_read_only){
                        fs = match SqliteFs::new(path) {
                            Ok(n) => n,
                            Err(err) => {println!("{:?}", err); return;}
                        };
                    }
                    else{
                        fs = match SqliteFs::new(path) {
                            Ok(n) => n,
                            Err(err) => {println!("{:?}", err); return;}
                        };
                    }
                }
            }
        }
        None => {
            let mut db = match Sqlite::new_in_memory() {
                Ok(n) => n,
                Err(err) => {println!("{:?}", err); return;}
            };
            match db.init() {
                Ok(n) => n,
                Err(err) => {println!("{:?}", err); return;}
            };
            fs = match SqliteFs::new_with_db(db) {
                Ok(n) => n,
                Err(err) => {println!("{:?}", err); return;}
            };
        }
    }
    match fuse::mount(fs, &mountpoint, &options) {
        Ok(n) => n,
        Err(err) => error!("{}", err)
    }
}
