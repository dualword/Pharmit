#!/usr/local/bin/python

#extract smiles for DTP/NCI compounds from PubChem with NSC identifiers

import sys,urllib2,json

sids = urllib2.urlopen('https://pubchem.ncbi.nlm.nih.gov/rest/pug/substance/sourceall/DTP.NCI/sids/TXT').read().split('\n')

#I couldn't figure out a why to get the NSC identifier and the isomeric smiels at the same time in one download

for sidline in sids:
    sid = sidline.strip()
    try:
		sidjson = json.load(urllib2.urlopen('https://pubchem.ncbi.nlm.nih.gov/rest/pug/substance/sid/%s/JSON'%sid))
		if len(sidjson) > 0:
			record = sidjson['PC_Substances'][0]
			nscid = 'NSC'+record['source']['db']['source_id']['str']
			clist = record['compound']
			for cl in clist:
				if cl['id']['type'] == 'standardized':
					cid = cl['id']['id']['cid']
					smile = urllib2.urlopen('https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/cid/%s/property/IsomericSMILES/TXT'%cid).read().strip()
					print smile,nscid
    except:
       sys.stderr.write('https://pubchem.ncbi.nlm.nih.gov/rest/pug/substance/sid/%s/JSON'%sid)        
