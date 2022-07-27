import json
from urllib.request import urlopen
from html.parser import HTMLParser
from boto3 import resource

EpisodeNumber = 0
EpisodeDictionary = {}
FoundTag=False
EpisodeList=[]
BigList=[]

class CIDRAPHTMLParser(HTMLParser):
    def handle_starttag(self, tag, attrs):
        global FoundTag,EpisodeNumber,EpisodeList
        if tag=='a':
            Temp=attrs[0][1]
            if  '/covid-19/podcasts-webinars/' in Temp or '/episode-1-how-we-got-here' in Temp:
                if Temp[:1]=='/':
                    Temp='https://www.cidrap.umn.edu'+Temp
                EpisodeNumber=EpisodeNumber+1
                EpisodeList.append(EpisodeNumber)
                EpisodeList.append(Temp)
                parser = CIDRAPHTMLParser()
                HTML= urlopen(Temp).read().decode('UTF-8')
                parser.feed(HTML)  
                FoundTag=True
        elif tag=='source':
            if attrs[0][1][-4:]=='.mp3':
                EpisodeList.append(attrs[0][1])
        
    def handle_data(self, data):
        global FoundTag, EpisodeList
        if FoundTag:
            EpisodeList.append(data)
            BigList.append(EpisodeList)
            EpisodeList=[]
            FoundTag=False

class MyHTMLParser(HTMLParser):
    def handle_starttag(self, tag, attrs):
        global EpisodeNumber
        if tag=='a':
            EpisodeNumber = attrs[0][1].replace('epi','').replace('.htm','')
            if EpisodeNumber.isnumeric():
                EpisodeNumber = int(EpisodeNumber)
            else:
                EpisodeNumber = 0

    def handle_data(self, data):
        global EpisodeNumber, EpisodeDictionary
        if not data.strip().isnumeric() and EpisodeNumber != 0:
            EpisodeDictionary[EpisodeNumber]=data.strip()
            EpisodeNumber = 0

def lambda_handler(event, context):
    global EpisodeNumber
    # with boto3.resource('dynamodb').Table('engines').batch_writer() as batch:
    #     for i in range(3500):
    #         batch.delete_item(Key={'EpisodeNumber': i})
    #     return
    
    with resource('dynamodb').Table('mmwr').batch_writer() as batch:
        for e in json.loads(urlopen('https://tools.cdc.gov/api/v2/resources/media/408549.json').read().decode('UTF-8'))['results']:
            for n,c in enumerate(e['children']):
                count=n+1
                for r in c['enclosures']:
                    if r['resourceUrl'][-4:]=='.mp3':
                        batch.put_item(Item={'EpisodeNumber': count
                            ,'Title': c['name']
                            ,'QRCode': str('https://tools.cdc.gov/medialibrary/index.aspx#/media/id/'+str(c['id'])).replace('/','\/')
                            ,'Audio': r['resourceUrl'].replace('/','\/')
                        })
        batch.put_item(Item={'EpisodeNumber': 0,'Title': str(count)})
    #return



    EpisodeNumber=0
    parser = CIDRAPHTMLParser()
    HTML= urlopen('https://www.cidrap.umn.edu/covid-19/podcasts-webinars').read().decode('UTF-8')
    parser.feed(HTML)
    with resource('dynamodb').Table('cidrap').batch_writer() as batch:
        for b in BigList:
            batch.put_item(Item={'EpisodeNumber': b[0]
                ,'Title': b[3]
                ,'QRCode': b[1].replace('/','\/')
                ,'Audio': b[2].replace('/','\/')
            })
        batch.put_item(Item={'EpisodeNumber': 0,'Title': str(EpisodeNumber)})
     
    
    #EpisodeNumber=0
    #return

    
    parser = MyHTMLParser()
    HTML= urlopen('https://uh.edu/engines/keywords.htm').read().decode('UTF-8')
    HTML = HTML.replace('<em>','').replace('</em>','').replace('<i>','').replace('</i>','')
    HTML = HTML.replace('<strong>','').replace('</strong>','').replace('<b>','').replace('</b>','')
    parser.feed(HTML)
    
    EpisodeDictionary[0] = str(list(EpisodeDictionary)[-1])
    EpisodeDictionary[2991] = 'Tswett and the Development of Chromatography'
    EpisodeDictionary[2992] = 'Algae'
    EpisodeDictionary[3107] = 'EXPECTATIONS OF BRILLIANCE'
    with resource('dynamodb').Table('engines').batch_writer() as batch:
        for e in EpisodeDictionary:
            batch.put_item(Item={'EpisodeNumber': e
                ,'Title': EpisodeDictionary[e]
                ,'QRCode': 'https:\/\/www.uh.edu\/engines\/epi' + str(e) + '.htm'
                ,'Audio':'http:\/\/www.kuhf.org\/programaudio\/engines\/eng' + str(e) + '_64k.mp3'
            })
