
/******************************************************************************
 * Copyright © 2014-2017 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/
//
//  LP_portfolio.c
//  marketmaker
//

cJSON *LP_portfolio_entry(struct iguana_info *coin)
{
    cJSON *item = cJSON_CreateObject();
    jaddstr(item,"coin",coin->symbol);
    jaddstr(item,"address",coin->smartaddr);
    jaddnum(item,"amount",dstr(coin->maxamount));
    jaddnum(item,"price",coin->price_kmd);
    jaddnum(item,"kmd_equiv",dstr(coin->kmd_equiv));
    jaddnum(item,"perc",coin->perc);
    jaddnum(item,"goal",coin->goal);
    jaddnum(item,"goalperc",coin->goalperc);
    jaddnum(item,"force",coin->force);
    jaddnum(item,"balanceA",dstr(coin->balanceA));
    jaddnum(item,"valuesumA",dstr(coin->valuesumA));
    if ( coin->valuesumA != 0 )
        jaddnum(item,"aliceutil",100. * (double)coin->balanceA/coin->valuesumA);
    jaddnum(item,"balanceB",dstr(coin->balanceB));
    jaddnum(item,"valuesumB",dstr(coin->valuesumB));
    if ( coin->valuesumB != 0 )
        jaddnum(item,"bobutil",100. * (double)coin->balanceB/coin->valuesumB);
    return(item);
}

uint64_t LP_balance(uint64_t *valuep,int32_t iambob,char *symbol,char *coinaddr)
{
    cJSON *array,*item; int32_t i,n; uint64_t valuesum,satoshisum;
    valuesum = satoshisum = 0;
    if ( (array= LP_inventory(symbol,iambob)) != 0 )
    {
        if ( (n= cJSON_GetArraySize(array)) > 0 && is_cJSON_Array(array) != 0 )
        {
            for (i=0; i<n; i++)
            {
                item = jitem(array,i);
                valuesum += j64bits(item,"value") + j64bits(item,"value2");
                satoshisum += j64bits(item,"satoshis");
            }
        }
        free_json(array);
    }
    *valuep = valuesum;
    return(satoshisum);
}

char *LP_portfolio()
{
    double strongest = 0.,goalsum = 0.; uint64_t kmdsum = 0; int32_t iter; cJSON *retjson,*array; struct iguana_info *coin,*tmp,*strongestcoin=0;
    array = cJSON_CreateArray();
    retjson = cJSON_CreateObject();
    for (iter=0; iter<2; iter++)
    {
        HASH_ITER(hh,LP_coins,coin,tmp)
        {
            if ( coin->inactive != 0 )
                continue;
            if ( iter == 0 )
            {
                coin->balanceA = LP_balance(&coin->valuesumA,0,coin->symbol,coin->smartaddr);
                coin->balanceB = LP_balance(&coin->valuesumB,1,coin->symbol,coin->smartaddr);
                if ( strcmp(coin->symbol,"KMD") != 0 )
                    coin->price_kmd = LP_price(coin->symbol,"KMD");
                else coin->price_kmd = 1.;
                coin->maxamount = coin->valuesumA;
                if ( coin->valuesumB > coin->maxamount )
                    coin->maxamount = coin->valuesumB;
                coin->kmd_equiv = coin->maxamount * coin->price_kmd;
                kmdsum += coin->kmd_equiv;
                goalsum += coin->goal;
            }
            else
            {
                if ( kmdsum > SMALLVAL )
                    coin->perc = 100. * coin->kmd_equiv / kmdsum;
                if ( goalsum > SMALLVAL && coin->goal > SMALLVAL )
                {
                    coin->goalperc = 100. * coin->goal / goalsum;
                    if ( (coin->force= (coin->goalperc - coin->perc)) < 0. )
                        coin->force *= -coin->force;
                    else coin->force *= coin->force;
                    if ( fabs(coin->force) > fabs(strongest) )
                    {
                        strongest = coin->force;
                        strongestcoin = coin;
                    }
                } else coin->goalperc = coin->force = 0.;
                jaddi(array,LP_portfolio_entry(coin));
            }
        }
    }
    jaddstr(retjson,"result","success");
    jaddnum(retjson,"kmd_equiv",dstr(kmdsum));
    if ( strongestcoin != 0 )
    {
        jaddstr(retjson,"strongest",strongestcoin->symbol);
        jaddnum(retjson,"force",strongest);
    }
    jadd(retjson,"portfolio",array);
    return(jprint(retjson,1));
}

char *LP_portfolio_goal(char *symbol,double goal)
{
    struct iguana_info *coin;
    if ( (coin= LP_coinfind(symbol)) != 0 && coin->inactive == 0 )
    {
        coin->goal = goal;
        return(LP_portfolio());
    } else return(clonestr("{\error\":\"cant set goal for inactive coin\"}"));
}

int32_t LP_autoprices,LP_autofills;

int32_t LP_autofill(char *base,char *rel,double maxprice,double totalrelvolume)
{
    struct LP_priceinfo *basepp,*relpp;
    if ( (basepp= LP_priceinfofind(base)) != 0 && (relpp= LP_priceinfofind(rel)) != 0 )
    {
        basepp->maxprices[relpp->ind] = maxprice;
        basepp->relvols[relpp->ind] = totalrelvolume;
        LP_autofills++;
        return(0);
    }
    return(-1);
}

int32_t LP_autoprice(char *base,char *rel,double minprice,double margin,char *type)
{
    struct LP_priceinfo *basepp,*relpp;
    if ( (basepp= LP_priceinfofind(base)) != 0 && (relpp= LP_priceinfofind(rel)) != 0 )
    {
        basepp->minprices[relpp->ind] = minprice;
        basepp->margins[relpp->ind] = margin;
        LP_autoprices++;
        return(0);
    }
    return(-1);
}

void LP_autopriceset(void *ctx,int32_t dir,struct LP_priceinfo *relpp,struct LP_priceinfo *basepp,double price)
{
    double margin,minprice; int32_t changed;
    if ( (margin= basepp->margins[relpp->ind]) != 0. )
    {
        if ( dir > 0 )
            price = 1. / (price * (1. - margin));
        else price = (price * (1. + margin));
        //printf("%s/%s %.8f dir.%d margin %.8f\n",relpp->symbol,basepp->symbol,price,dir,margin);
        if ( (minprice= basepp->minprices[relpp->ind]) == 0. || price >= minprice )
        {
            LP_mypriceset(&changed,relpp->symbol,basepp->symbol,price);
            //printf("changed.%d\n",changed);
            if ( changed != 0 )
                LP_pricepings(ctx,LP_myipaddr,LP_mypubsock,relpp->symbol,basepp->symbol,price);
        }
    }
}

double LP_pricesparse(void *ctx,int32_t trexflag,char *retstr,struct LP_priceinfo *btcpp)
{
    //{"success":true,"message":"","result":[{"MarketName":"BTC-KMD","High":0.00040840,"Low":0.00034900,"Volume":328042.46061669,"Last":0.00037236,"BaseVolume":123.36439511,"TimeStamp":"2017-07-15T13:50:21.87","Bid":0.00035721,"Ask":0.00037069,"OpenBuyOrders":343,"OpenSellOrders":1690,"PrevDay":0.00040875,"Created":"2017-02-11T23:04:01.853"},
    //{"TradePairId":4762,"Label":"WAVES/BTC","AskPrice":0.00099989,"BidPrice":0.00097350,"Low":0.00095000,"High":0.00108838,"Volume":6501.24403100,"LastPrice":0.00098028,"BuyVolume":1058994.86554882,"SellVolume":2067.87377158,"Change":-7.46,"Open":0.00105926,"Close":0.00098028,"BaseVolume":6.52057452,"BuyBaseVolume":2.33098660,"SellBaseVolume":1167.77655709},
    int32_t i,j,n,iter; double price,kmdbtc,bid,ask,nxtkmd=0.; struct LP_priceinfo *coinpp,*refpp; char symbol[16],*name,*refcoin; cJSON *retjson,*array,*item;
    if ( (retjson= cJSON_Parse(retstr)) != 0 )
    {
        //printf("got.(%s)\n",retstr);
        kmdbtc = 0.;
        refcoin = "BTC";
        refpp = btcpp;
        if ( (array= jarray(&n,retjson,trexflag != 0 ? "result" : "Data")) != 0 )
        {
            for (iter=0; iter<2; iter++)
            {
                for (i=0; i<n; i++)
                {
                    item = jitem(array,i);
                    if ( (name= jstr(item,trexflag != 0 ? "MarketName" : "Label")) != 0 )
                    {
                        symbol[0] = 0;
                        if ( trexflag != 0 )
                        {
                            if ( strncmp("BTC-",name,4) == 0 )
                            {
                                name += 4;
                                strcpy(symbol,name);
                            }
                        }
                        else
                        {
                            for (j=0; j<sizeof(symbol)-1; j++)
                                if ( (symbol[j]= name[j]) == '/' )
                                    break;
                            symbol[j] = 0;
                            if ( strcmp(name+j+1,"BTC") != 0 )
                                continue;
                        }
                        if ( symbol[0] != 0 )
                        {
                            //printf("%s\n",jprint(item,0));
                            bid = jdouble(item,trexflag != 0 ? "Bid" : "BidPrice");
                            ask = jdouble(item,trexflag != 0 ? "Ask" : "AskPrice");
                            if ( iter == 1 && kmdbtc > SMALLVAL && strcmp(symbol,"NXT") == 0 )
                                nxtkmd = 0.5 * (bid + ask) / kmdbtc;
                            if ( (coinpp= LP_priceinfofind(symbol)) != 0 )
                            {
                                coinpp->high[trexflag] = jdouble(item,"High");
                                coinpp->low[trexflag] = jdouble(item,"Low");
                                //coinpp->volume = jdouble(item,"Volume");
                                //coinpp->btcvolume = jdouble(item,"BaseVolume");
                                coinpp->last[trexflag] = jdouble(item,trexflag != 0 ? "Last" : "LastPrice");
                                coinpp->bid[trexflag] = bid;
                                coinpp->ask[trexflag] = ask;
                                //coinpp->prevday = jdouble(item,"PrevDay");
                                //printf("iter.%d trexflag.%d %s high %.8f, low %.8f, last %.8f hbla.(%.8f %.8f)\n",iter,trexflag,symbol,coinpp->high[trexflag],coinpp->low[trexflag],coinpp->last[trexflag],coinpp->bid[trexflag],coinpp->ask[trexflag]);
                                if ( coinpp->bid[trexflag] > SMALLVAL && coinpp->ask[trexflag] > SMALLVAL )
                                {
                                    price = 0.5 * (coinpp->bid[trexflag] + coinpp->ask[trexflag]);
                                    if ( iter == 0 )
                                    {
                                        if ( strcmp(symbol,"KMD") == 0 )
                                            kmdbtc = price;
                                    }
                                    else
                                    {
                                        if ( strcmp(symbol,"KMD") == 0 )
                                            continue;
                                        //printf("(%s/%s) iter.%d trexflag.%d %s %.8f %.8f\n",refpp->symbol,coinpp->symbol,iter,trexflag,symbol,price,price/kmdbtc);
                                        price /= kmdbtc;
                                    }
                                    if ( trexflag == 0 && coinpp->bid[1] > SMALLVAL && coinpp->ask[1] > SMALLVAL )
                                    {
                                        //printf("have trex: iter.%d trexflag.%d %s %.8f %.8f\n",iter,trexflag,symbol,coinpp->bid[1],coinpp->ask[1]);
                                        continue;
                                    }
                                    LP_autopriceset(ctx,1,refpp,coinpp,price);
                                    LP_autopriceset(ctx,-1,coinpp,refpp,price);
                                }
                            }
                        }
                    }
                }
                refcoin = "KMD";
                if ( kmdbtc == 0. || (refpp= LP_priceinfofind("KMD")) == 0 )
                    break;
            }
        }
        free_json(retjson);
    }
    return(nxtkmd);
}

static char *assetids[][3] =
{
    { "12071612744977229797", "UNITY", "10000" },
    { "15344649963748848799", "DEX", "1" },
    { "6883271355794806507", "PANGEA", "10000" },
    { "17911762572811467637", "JUMBLR", "10000" },
    { "17083334802666450484", "BET", "10000" },
    { "13476425053110940554", "CRYPTO", "1000" },
    { "6932037131189568014", "HODL", "1" },
    { "3006420581923704757", "SHARK", "10000" },
    { "17571711292785902558", "BOTS", "1" },
    { "10524562908394749924", "MGW", "1" },
};

void prices_loop(void *ignore)
{
    char *retstr; cJSON *retjson,*bid,*ask; uint64_t bidsatoshis,asksatoshis; int32_t i; double nxtkmd,price; struct LP_priceinfo *btcpp,*kmdpp,*fiatpp,*nxtpp; void *ctx = bitcoin_ctx();
    while ( 1 )
    {
        if ( LP_autoprices == 0 )
        {
            sleep(60);
            continue;
        }
        if ( (btcpp= LP_priceinfofind("BTC")) == 0 )
        {
            sleep(60);
            continue;
        }
        if ( (retstr= issue_curlt("https://bittrex.com/api/v1.1/public/getmarketsummaries",LP_HTTP_TIMEOUT*10)) == 0 )
        {
            printf("error getting marketsummaries\n");
            sleep(60);
            continue;
        }
        nxtkmd = LP_pricesparse(ctx,1,retstr,btcpp);
        free(retstr);
        if ( (retstr= issue_curlt("https://www.cryptopia.co.nz/api/GetMarkets",LP_HTTP_TIMEOUT*10)) == 0 )
        {
            printf("error getting marketsummaries\n");
            sleep(60);
            continue;
        }
        LP_pricesparse(ctx,0,retstr,btcpp);
        free(retstr);
        if ( (kmdpp= LP_priceinfofind("KMD")) != 0 )
        {
            for (i=0; i<32; i++)
            {
                if ( (fiatpp= LP_priceinfofind(CURRENCIES[i])) != 0 )
                {
                    if ( (retjson= LP_paxprice(CURRENCIES[i])) != 0 )
                    {
                        //printf("(%s %.8f %.8f) ",CURRENCIES[i],jdouble(retjson,"price"),jdouble(retjson,"invprice"));
                        price = jdouble(retjson,"price");
                        LP_autopriceset(ctx,1,kmdpp,fiatpp,price);
                        LP_autopriceset(ctx,-1,fiatpp,kmdpp,price);
                        free_json(retjson);
                    }
                }
            }
        }
        if ( nxtkmd > SMALLVAL )
        {
            for (i=0; i<sizeof(assetids)/sizeof(*assetids); i++)
            {
                if ( (nxtpp= LP_priceinfofind(assetids[i][1])) != 0 )
                {
                    price = 0.;
                    bidsatoshis = asksatoshis = 0;
                    if ( (retjson= LP_assethbla(assetids[i][0])) != 0 )
                    {
                        if ( (bid= jobj(retjson,"bid")) != 0 && (ask= jobj(retjson,"ask")) != 0 )
                        {
                            bidsatoshis = j64bits(bid,"priceNQT") * atoi(assetids[i][2]);
                            asksatoshis = j64bits(ask,"priceNQT") * atoi(assetids[i][2]);
                            if ( bidsatoshis != 0 && asksatoshis != 0 )
                                price = 0.5 * dstr(bidsatoshis + asksatoshis) * nxtkmd;
                        }
                        LP_autopriceset(ctx,1,kmdpp,nxtpp,price);
                        LP_autopriceset(ctx,-1,nxtpp,kmdpp,price);
                        //printf("%s %s -> (%s) nxtkmd %.8f %.8f %.8f\n",assetids[i][1],assetids[i][0],jprint(retjson,0),nxtkmd,0.5*dstr(bidsatoshis + asksatoshis),price);
                        free_json(retjson);
                    }
                }
            }
        }
        if ( LP_autofills > 0 )
        {
            char *strongest; struct iguana_info *coin;
            if ( (retstr= LP_portfolio()) != 0 )
            {
                if ( (retjson= cJSON_Parse(retstr)) != 0 )
                {
                    if ( (strongest= jstr(retjson,"strongest")) != 0 && (coin= LP_coinfind(strongest)) != 0 )
                    {
                        printf("strongest.%s force %f\n",strongest,jdouble(retjson,"force"));
                    }
                    free_json(retjson);
                }
                free(retstr);
            }
        }
        sleep(60);
    }
}


