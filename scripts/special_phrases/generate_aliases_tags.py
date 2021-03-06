#!/usr/bin/env python3

# Run this script from project root directory

import requests, unicodedata, collections, json

# output file names or prefix
tag_alias_json = "data/geocoder-npl-tag-aliases.json"
doc_prefix = "docs/tags/"

### ignored tags
geotags_to_ignore = [ 'amenity_brothel',
                      'shop_erotic' ]

### alias -> tag transfer and tag -> alias
A2T = {}
T2A = {}
Geo2OSM = {}

# helper function
def san(txt):
    return txt.replace('_', '\_')

# start import
findex = open(doc_prefix + "index.md", "w")
findex.write("# Tags and their aliases as used in OSM Scout Server\n\n")
findex.write('Language | Aliases | Geocoder Tags \n --- | ---:| ---: \n')

for language in [ "af","ar","br","ca","cs","de","de_at","en","es","et","eu","fa","fi","fr","gl","hr","hu",
                 "ia","is","it","ja","mk","nl","no","pl","ps","pt","ru","sk","sl","sv","uk","vi" ]:

#for language in [ "af" ]:

    print("Language", language)
    alias2tag = collections.defaultdict(list)
    tag2alias = {}

    r = str(requests.get('https://wiki.openstreetmap.org/wiki/Special:Export/Nominatim/Special_Phrases/' + language.upper()).text)
    r = r[ r.find('{| '): ]

    for i in r.split('|-')[1:]:
        i = i.strip()
        n = i.split('||')
        if len(n) != 5:
            continue
        k = []
        for j in n: k.append(j.strip().replace('&quot;',''))
        if k[0].find('|') == 0:
            k[0] = k[0][1:].strip()
        use = (k[-2] == '-') # and k[2].find('"')<0)
        if use:
            if k[2] == 'yes': geotag=k[1]
            else: geotag = k[1] + '_' + k[2]
            alias = k[0]
            singular = (k[-1] == 'N')
            if geotag in geotags_to_ignore: continue

            alias2tag[k[0]].append( geotag )

            if singular and geotag not in tag2alias:
                tag2alias[geotag] = k[0]

            Geo2OSM[geotag] = k[1] + '=' + k[2]

    A2T[language] = alias2tag
    T2A[language] = tag2alias

    fname_t2a = "tag2alias_" + language + ".md"
    with open(doc_prefix + fname_t2a, "w") as f:
        f.write('# ' + language.upper() + ': Tag and its main alias, tag -> alias\n\n')
        f.write('Tag | Alias \n')
        f.write('--- | --- \n')
        keys = list(tag2alias.keys())
        keys.sort()
        for k in keys:
            f.write('[' + san(k) + '](https://taginfo.openstreetmap.org/tags/' + Geo2OSM[k] + ') | ' + tag2alias[k] + '\n')

    fname_a2t = "alias2tag_" + language + ".md"
    with open(doc_prefix + fname_a2t, "w") as f:
        f.write('# ' + language.upper() + ': Aliases and the corresponding tags, alias -> tag\n\n')
        f.write('Alias | Tag(s) \n')
        f.write('--- | --- \n')
        keys = list(alias2tag.keys())
        keys.sort()
        for k in keys:
            f.write(k + ' | ')
            for t in alias2tag[k]:
                f.write(' [' + san(t) + '](https://taginfo.openstreetmap.org/tags/' + Geo2OSM[t] + ')')
            f.write('\n')

    findex.write( language.upper() + ' | [' +
                  str(len(alias2tag.keys())) + ' aliases](' + fname_a2t + ') | [' +
                  str(len(tag2alias.keys())) + ' tags](' + fname_t2a + ') \n')



print("Saving to JSON")
json.dump( { "tag2alias": T2A, "alias2tag": A2T },
           open(tag_alias_json, "w"), sort_keys=True, indent=0 )

findex.write("\n\nIndex generated by `scripts/special_phrases/generate_aliases_tags.py`\n")

print("Done")
