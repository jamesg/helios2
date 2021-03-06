var Album = Backbone.Model.extend(
        {
            defaults: {
                name: ''
            },
            url: function() {
                return this.isNew() ? '/api/album' : ('/api/album/' + this.id);
            }
        }
        );

var AlbumCollection = Backbone.Collection.extend(
        {
            idAttribute: 'id',
            model: Album,
            comparator: 'name',
            url: '/api/album',
            add: function(model) {
                 return (this.any(function(model_) { return model.id == model_.id })) ?
                     false :
                     Backbone.Collection.prototype.add.apply(this, arguments);
            }
        }
        );

var AlbumFormView = StaticView.extend(
        {
            className: 'album-form',
            template: '<h1>Album</h1>' +
                '<form class="aligned-form">' +
                '<label for="name">Name</label><input type="text" name="name" value="<%-name%>"></input><br>' +
                '<button type="submit" class="hidden"></button>' +
                '</form>',
            events: {
                'click button': function() { this.save(); this.trigger('finished'); }
            },
            save: function() {
                var col = this.model.collection;
                this.model.save(
                    {
                        name: this.$('[name=name]').val()
                    },
                    {
                        success: function() {
                            if(col)
                                col.fetch();
                        },
                        wait: true
                    }
                    );
            }
        }
        );

var Photograph = Backbone.Model.extend(
        {
            defaults: {
                title: '',
                caption: '',
                location: '',
                taken: '',
                starred: false
            },
            url: function() {
                return this.isNew() ? '/api/photograph' : ('/api/photograph/' + this.id);
            }
        }
        );

var PhotographCollection = Backbone.Collection.extend(
        {
            model: Photograph,
            comparator: 'taken'
            // No URL attribute is defined because photograph collections can
            // be rerieved from various URLs.
        }
        );

var Tag = Backbone.Model.extend(
        {
            defaults: {
                count: '',
                tag: ''
            }
        }
        );

var TagCollection = Backbone.Collection.extend(
        {
            model: Tag,
            url: '/api/tag'
        }
        );

var PhotographFormView = StaticView.extend(
        {
            className: 'photograph-form',
            template: '<h1>Photograph</h1>' +
                '<form class="aligned-form">' +
                '<label for="title">Title</label><input type="text" name="title" value="<%=title%>"></input><br>' +
                '<label for="caption">Caption</label><textarea id="caption-textarea" name="caption" rows="4"><%=caption%></textarea><br>' +
                '<label for="taken">Date</label><input type="text" name="taken" value="<%-taken%>"></input><br>' +
                '<label for="location">Location</label><input type="text" name="location" value="<%-location%>"></input><br>' +
                // TODO tags, albums
                '<button type="submit" class="hidden"></button>' +
                '</form>',
            events: {
                'click button': function() { this.save(); this.trigger('finished'); }
            },
            save: function() {
                this.model.save(
                    {
                        title: this.$('[name=title]').val(),
                        caption: this.$('[name=caption]').val(),
                        taken: this.$('[name=taken]').val(),
                        location: this.$('[name=location]').val()
                    },
                    {
                        //success: function() {
                            //if(col)
                                //col.fetch();
                        //},
                        wait: true
                    }
                    );
            }
        }
        );

var ThumbnailView = StaticView.extend(
        {
            tagName: 'li',
            className: 'thumbnail',
            template: '<a href="<%-targetUrl%>"><img src="/photograph/small/<%-id%>" alt="<%-title%>"></img><span class="vertical-align-helper"></span></a>',
            templateParams: function() {
                return {
                    title: this.model.get('title'),
                    targetUrl: this.targetUrl,
                    id: this.model.id
                };
            },
            events: {
                click: function() {
                    this.trigger('click')
                }
            }
        }
        );

var photographInAlbumUrl = function(photographId, albumId) {
    return 'photograph.html#' + photographId + '.inalbum.' + albumId;
};

var photographInMonthUrl = function(photographId, fullMonth) {
    return 'photograph.html#' + photographId + '.inmonth.' + fullMonth;
};

var photographInCollectionUrl = function(collectionType, photographId, collectionId) {
    switch(collectionType) {
        case 'inalbum':
            return photographInAlbumUrl(photographId, collectionId);
        case 'inmonth':
            return photographInMonthUrl(photographId, collectionId);
    }
};

