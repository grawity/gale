#include "common.h"
#include "key.h"
#include "id.h"

void export_auth_id(struct auth_id *id,struct gale_data *data,int private) {
	if (private)
		_ga_export_priv(id,data);
	else
		_ga_export_pub(id,data,EXPORT_NORMAL);
}

void import_auth_id(struct auth_id **id,struct gale_data data,int private) {
	if (private)
		_ga_import_priv(id,data,NULL);
	else {
		_ga_import_pub(id,data,NULL,IMPORT_NORMAL);
		if (*id && !_ga_trust_pub(*id)) *id = NULL;
	}
}
