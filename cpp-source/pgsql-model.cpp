#include <string>
#include <vector>

#include <pqxx/pqxx>

#include "cryptopp/aes.h"
#include "cryptopp/osrng.h"
#include "cryptopp/modes.h"
#include "cryptopp/base64.h"
#include "cryptopp/filters.h"
#include "cryptopp/cryptlib.h"

#include "util.hpp"
#include "json.hpp"
#include "pgsql-model.hpp"

PgSqlModel::PgSqlModel(std::string new_conn, std::string new_table, std::vector<Column*> new_cols, unsigned char new_access_flags)
:table(new_table), access_flags(new_access_flags), conn(new_conn), cols(new_cols), page_size(10){
	this->num_insert_values = 0;
	for(size_t i = 0; i < this->cols.size(); ++i){
		if(!(this->cols[i]->flags & COL_AUTO) &&
		Util::strict_compare_inequal(this->cols[i]->name, "owner_id", 8)){
			this->num_insert_values++;
		}
	}
}

static pqxx::result SqlWrap(pqxx::work* txn, std::string sql){
	std::cout << "PSQL: |" << sql << "|" << std::endl;
	pqxx::result res = txn->exec(sql);
	txn->commit();
	return res;
}

JsonObject* PgSqlModel::Error(std::string message){
	JsonObject* error = new JsonObject(OBJECT);
	error->objectValues["error"] = new JsonObject(message);
	return error;
}

JsonObject* PgSqlModel::ResultToJson(pqxx::result* res){
	JsonObject* result_json = new JsonObject(ARRAY);
	for(pqxx::result::size_type i = 0; i < res->size(); ++i){
		JsonObject* next_item = new JsonObject(OBJECT);
		for(size_t j = 0; j < this->cols.size(); ++j){
			if(!(this->cols[j]->flags & COL_HIDDEN)){
				next_item->objectValues[this->cols[j]->name] = new JsonObject(STRING);
				next_item->objectValues[this->cols[j]->name]->stringValue = (*res)[i][this->cols[j]->name].as<std::string>();
			}
		}
		result_json->arrayValues.push_back(next_item);
	}
	return result_json;
}

JsonObject* PgSqlModel::ResultToJson(pqxx::result::tuple row){
	JsonObject* result_json= new JsonObject(OBJECT);
	for(size_t j = 0; j < this->cols.size(); ++j){
		if(!(this->cols[j]->flags & COL_HIDDEN)){
			result_json->objectValues[this->cols[j]->name] = new JsonObject(STRING);
			result_json->objectValues[this->cols[j]->name]->stringValue = row[this->cols[j]->name].as<std::string>();
		}
	}
	return result_json;
}

JsonObject* PgSqlModel::All(int page){
	std::string offset = std::to_string(page * this->page_size);
	pqxx::work txn(this->conn);
	pqxx::result res = SqlWrap(&txn, "SELECT * FROM " + this->table + " ORDER BY id LIMIT " + std::to_string(this->page_size) + " OFFSET " + offset + ";");
	return this->ResultToJson(&res);
}

JsonObject* PgSqlModel::Where(std::string key, std::string value, int page){
	if(!this->HasColumn(key)){
		return Error("Bad key.");
	}

	std::string offset = std::to_string(page * this->page_size);
	pqxx::work txn(this->conn);
	pqxx::result res = SqlWrap(&txn, "SELECT * FROM " + this->table + " WHERE " + key + " = " + txn.quote(value) + " ORDER BY created_on DESC LIMIT " + std::to_string(this->page_size) + " OFFSET " + offset + ";");
	
	if(res.size() == 0){
		return Error("Bad value.");
	}
	return this->ResultToJson(&res);
}

JsonObject* PgSqlModel::Insert(std::vector<JsonObject*> values){
	pqxx::work txn(this->conn);
	std::stringstream sql;
	sql << "INSERT INTO " << this->table << " (" ;
	for(size_t i = 0; i < this->cols.size(); ++i){
		if(i < this->cols.size() - 1 && !(this->cols[i + 1]->flags & COL_AUTO)){
			sql << this->cols[i]->name << ", ";
		}else{
			sql << this->cols[i]->name << ") VALUES (DEFAULT, ";
			break;
		}
	}
	for(size_t i = 0; i < values.size(); ++i){
		if(i < values.size() - 1){
			sql << txn.quote(values[i]->stringValue) << ", ";
		}else{
			sql << txn.quote(values[i]->stringValue) << ") RETURNING id;";
		}
	}
	try{
		pqxx::result res = SqlWrap(&txn, sql.str());
		
		JsonObject* result = new JsonObject(OBJECT);
		result->objectValues["id"] = new JsonObject(res[0][0].as<const char*>());
		return result;
	}catch(const pqxx::pqxx_exception &e){
		PRINT(e.base().what())
		return Error("You provided incomplete or bad data.");
	}
}

bool PgSqlModel::HasColumn(std::string name){
	for(size_t i = 0; i < this->cols.size(); ++i){
		if(this->cols[i]->name == name){
			return true;
		}
	}
	return false;
}

/*
static void result_print(pqxx::result result){
	PRINT("RESULTS:")
	for(auto row : result){
		PRINT("ROW:")
		for(auto col : row){
			PRINT(col.as<const char*>())
		}
	}
}
*/

bool PgSqlModel::IsOwner(std::string id, std::string owner_id){
	pqxx::work txn(this->conn);
	pqxx::result res = SqlWrap(&txn, "SELECT * FROM " + this->table + " WHERE id = " + txn.quote(id) + ';');
	
	if(res[0]["owner_id"].as<std::string>() == owner_id){
		return true;
	}
	return false;
}

JsonObject* PgSqlModel::Update(std::string id, std::unordered_map<std::string, JsonObject*> values){
	pqxx::work txn(this->conn);
	std::stringstream sql;
	sql << "UPDATE " << this->table << " SET ";
	for(auto iter = values.begin(); iter != values.end(); ++iter){
		if(!this->HasColumn(iter->first)){
			return Error("Bad key.");
		}
		sql << iter->first << " = " + txn.quote(iter->second->stringValue);
		if(std::next(iter) != values.end()){
			sql << ", ";
		}
	}
	sql << " WHERE id = " + txn.quote(id) + " RETURNING id;";
	try{
		pqxx::result res = SqlWrap(&txn, sql.str());
		
		JsonObject* result = new JsonObject(OBJECT);
		result->objectValues["id"] = new JsonObject(res[0][0].as<const char*>());
		return result;
	}catch(const std::exception& e){
		PRINT(e.what())
		return Error("You provided incomplete or bad data.");
	}
}

JsonObject* PgSqlModel::Access(const std::string& key, const std::string& value, const std::string& password){
	try{
		pqxx::work txn(this->conn);
		pqxx::result res = SqlWrap(&txn, "SELECT * FROM " + this->table + " WHERE " + key + " = " + txn.quote(value) + ';');
	
		if(res.size() == 0){
			return Error("Bad username.");
		}else if(res[0]["password"].as<std::string>() == password){
			PRINT("DB: " << res[0]["password"].as<std::string>())
			PRINT("PS: " << password)
			return this->ResultToJson(res[0]);
		}else{
			PRINT("DB: " << res[0]["password"].as<std::string>())
			PRINT("PS: " << password)
			return Error("Bad password.");
		}
	}catch(const std::exception &e){
		PRINT(e.what())
		return Error("Bad data.");
	}
}
