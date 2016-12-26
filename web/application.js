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
                console.log('add', model);
                 return (this.any(function(model_) { console.log('test', model_); return model.id == model_.id })) ?
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

var AlbumView = StaticView.extend(
        {
            tagName: 'li',
            className: 'album',
            template: '<span class="album-name"><%-name%></span>',
            events: {
                click: function() { this.trigger('click'); }
            }
        }
        );

var Photograph = Backbone.Model.extend(
        {
            defaults: {
                title: '',
                location: '',
                taken: ''
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
                tag: ''
            }
        }
        );

var TagCollection = Backbone.Collection.extend(
        {
            model: Tag
        }
        );

var PhotographFormView = StaticView.extend(
        {
            className: 'photograph-form',
            template: '<h1>Photograph</h1>' +
                '<form class="aligned-form">' +
                '<label for="title">Title</label><input type="text" name="title" value="<%=title%>"></input><br>' +
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

var PhotographView = StaticView.extend(
        {
            template: '<img src="/photograph/medium/<%-id%>" alt="<%-title%>"></img><br><button name="edit">Edit</button>',
            events: {
                'click button[name=edit]': function() {
                    (new Modal({
                        view: PhotographFormView,
                        model: this.model,
                        buttons: [
                            StandardButton.cancel(),
                            StandardButton.destroy(
                                function() {
                                    new ConfirmModal(
                                            {
                                                message: 'Are you sure you want to delete this photograph?',
                                                callback: (function() {
                                                    this.model.destroy();
                                                    this.remove();
                                                }).bind(this)
                                            }
                                            );
                                }
                                ),
                            StandardButton.save(
                                function() {
                                    this.view.save();
                                    this.remove();
                                }
                                )
                        ]
                    }));
                }
            }
        }
        );

